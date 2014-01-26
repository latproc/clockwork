<?php require_once('settings.php');
/*
  Copyright (C) 2012 Martin Leadbeater, Michael O'Connor

  This file is part of Latproc

  Latproc is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.
  
  Latproc is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Latproc; if not, wri∂te to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/*
 *  iod client
 *  sends requests to the server to get the status of each point,
 *  displays the result in user-defined tabs, along with control buttons
 */

define("REQUEST_TIMEOUT", 100);

if ((!isset($user))) {
	// not logged in, show a link to the login page
	header('Location: admin.php');

print <<<EOD
<!DOCTYPE html PUBLIC 
  "-//W3C//DTD HTML 4.01 Transitional//EN" 
  "http://www.w3.org/TR/html4/loose.dtd" >

<html> <head> 
</head>
<body>
<div>Please <a href="login.php">Login</a></div>
</body></html>
EOD;
}
else {
	
$debug_messages = "";
$use_ajax = true; // the 'false' version of this may not actually work

$banner = "<p>Logged in as " . $user->getName() . "."
	. " <a href=\"admin.php?m=logout\">Logout</a>";
if ($user->isAdministrator()) {
	$banner .= ' <a href="admin.php?registration">Administration</a>';
}

// static image generator
function image_html($name, $filename, $item_id = false) {
	if ($item_id != false) $id = "id=\"${item_id}\"";
	else $id = "";
	return '<img width=20 name="'.$name.'" '.$id.' src="img/' .  $filename . '"/>';
}

// toggle button html generator
function button_image($name, $filename, $item_id = false) {
	global $use_ajax;
	$id = "";
	if ($item_id != false) $id = "id=\"$item_id\"";
	if ($use_ajax)
		return '<img width=20 class="out" '.$id. ' name="'.$name.'" src="img/' .  $filename . '"/>'. "\n";
	else
		return '<input type="image" width=20 '.$id .' name="'.$name.'" src="img/' .  $filename . '"/>';
}


// ----------------- utility functions -------------
function display_json_error($res, $reply) {
	global $debug_messages;
	switch ($res) {
        case JSON_ERROR_DEPTH:
            $err =  ' - Maximum stack depth exceeded';
        break;
        case JSON_ERROR_STATE_MISMATCH:
            $err =  ' - Underflow or the modes mismatch';
        break;
        case JSON_ERROR_CTRL_CHAR:
            $err =  ' - Unexpected control character found';
        break;
        case JSON_ERROR_SYNTAX:
            $err =  ' - Syntax error, malformed JSON';
        break;
        case JSON_ERROR_UTF8:
            $err =  ' - Malformed UTF-8 characters, possibly incorrectly encoded';
        break;
        default:
            $err =  ' - Unknown error';
        break;
    }
	$debug_messages .= $err . "<br/>" . $reply . "<br/>";
}
// ----------------------------------------------------

// build an html options list from a list of slaves, optionally with a member selected
function get_slaves_options($slaves, $selected=false) {
	$slaves_options = "<select>";
	if (!$selected) $slaves_options .= "<option> - please select -</option>";
	foreach ($slaves as $slave) {
		$slaves_options .= "<option value=" . $slave->position;
		if ($selected === $slave->position)
			 $slaves_options .= ' selected="selected">';
		else
			$slaves_options .= ">";
		$slaves_options .= $slave->position . " - " . $slave->name . " " . $slave->drawn_current . "mA</option>";
	}
	$slaves_options .= "</select>";
	return $slaves_options;
}

function zmqrequest($client, $request) {
	$read = $write = array();
	$client->send($request);
	$poll = new ZMQPoll();
	$poll->add($client, ZMQ::POLL_IN);
	$events = $poll->poll($read, $write, REQUEST_TIMEOUT);
	if ($events > 0) {
		return $client->recv();
	}
	return false;
}


//             Program starts here 

//         PART I - collect data

$header_extras='';
$page_body='';

$context = new ZMQContext();
$requester = new ZMQSocket($context, ZMQ::SOCKET_REQ);
$requester->connect("tcp://localhost:5555");
$requester->setSockOpt(ZMQ::SOCKOPT_LINGER, 0);
$requester->setSockOpt(ZMQ::SOCKOPT_BACKLOG, 1);

if (isset($_REQUEST["messages"])) {
	$reply = zmqrequest($requester,'MESSAGES ' . $_REQUEST["messages"]);
	echo $reply;
	return;
}
if (isset($_REQUEST["quit"])) {
	$reply = zmqrequest($requester,'QUIT');
	$debug_messages = $reply;
}
if (isset($_REQUEST["send"])) {
	$reply = zmqrequest($requester,'SEND ' . $_REQUEST["send"]);
	echo $reply;
	return;
}
if (isset($_REQUEST["enable"])) {
	$reply = zmqrequest($requester,'ENABLE ' . $_REQUEST["enable"]);
	echo $reply;
	return;
}
if (isset($_REQUEST["disable"])) {
	$reply = zmqrequest($requester,'DISABLE ' . $_REQUEST["disable"]);
	echo $reply;
	return;
}
if (isset($_REQUEST["setproperty"])) {
	$reply = zmqrequest($requester,'SET ' . $_REQUEST["setproperty"]);
	echo $reply;
	return;
}
if (isset($_REQUEST["describe"])) {
	$reply = zmqrequest($requester,'DESCRIBE ' . $_REQUEST["describe"] . " JSON");
	echo $reply;
	return;
}
/* 
	retrieve a list of the points to define the initial layout;after this, the 
	javascript updates the status but the layout is not changed unless a refresh
	is done.
 */

$current_tab = "";
if (isset($_REQUEST["list"]) && isset($_REQUEST["tab"])) {
	$current_tab = $_REQUEST["tab"];
}

$reply = zmqrequest($requester,'LIST JSON' . ' ' . $current_tab);
$config_entries_json = $reply;
$config_entries = json_decode($reply);
$res = json_last_error();
if (json_last_error() != JSON_ERROR_NONE) {
	display_json_error($res, $reply);
	$config_entries = array();
}
//$debug_messages .= "\n" . count($config_entries) . " items found\n";
//$debug_messages .= var_export($config_entries, true);

//$debug_messages .= "Request: " . var_export($_REQUEST, true) . "\n";
/*
	AJAX handlers for a status update and other commands
 */

if (isset($_REQUEST["list"])) {
	echo $config_entries_json;
	return;
}

// note: below is ignored if an AJAX request was received

/*
	obtain the master information, if available. This is general
	status information and is displayed on a 'Master' tab
 */
$reply = zmqrequest($requester,'MASTER');
$master = json_decode($reply);
$res = json_last_error();
if (json_last_error() != JSON_ERROR_NONE) {
	display_json_error($res, $reply);
	$master = false;
}

/*
	obtain a list of EtherCat bus Slaves. These are displayed in an options
	list in a 'Modules' tab, to help link the configured machines to the
	devices actually available on the bus.
 */
$reply = zmqrequest($requester,'SLAVES');
$slaves = json_decode($reply);
$res = json_last_error();
if (json_last_error() != JSON_ERROR_NONE) {
	display_json_error($res, $reply);
	$slaves = array();
}
$debug_messages .= "<literal>";
$debug_messages .= " " . count($slaves) . " slaves found\n";
$debug_messages .= $reply . "</literal><br>\n";

/* each tab has a 'refresh' button that refreshes all tabs and navigates
	back to the tab the user was on. This can be improved with a bit of
	javascripting one day.
 */
$anchor="";
if (isset($_REQUEST["anchor"])) $anchor='#'. $_REQUEST["anchor"];

/* 
	Handle an etherlab tool commad, entered on the Master tab
 */
if (isset($_REQUEST["query"])) {
	$q = $_REQUEST["query"];
	$reply = zmqrequest($requester,'EC ' . $q);
	$debug_messages .= "EC " . $q . ": " . $reply . "\n";
	usleep(100000); // TBD is this necessary any more?
}

/*
	Handle a user-click on a toggle button for an output
 */

if (isset($_REQUEST["toggle"])) {
	$point = $_REQUEST["toggle"];
	$reply = zmqrequest($requester,'TOGGLE ' . $point);
	$debug_messages .= "TOGGLE " . $point . ": " . $reply . "\n";
	usleep(100000);
	echo "$reply"; // ajax version
	return; // ajax version
}

/* if we aren't using ajax, we can get an automatic page refresh 
	by use of a redirect. This can probably be removed as the 
	only required bit is the 'else' clause.
 */
$siteurl="monitor.php" . $anchor;
if (isset($_REQUEST["s"])) {
	$s = intval($_REQUEST["s"]) - 1;
	if ($use_ajax) {
		header('Location: ' . $siteurl);
	}
	else {
	/* Crazy refresh logic to be reimplemented .... 
		$debug_messages .= $s . "</br>";
		if (time() - $s < 8) {
			//header('Location: /admin.php');
			//$header_extras='<meta http-equiv="REFRESH" content="1;url=">';
			$header_extras='<meta http-equiv="REFRESH" content="1;url=/' . $siteurl . '?s=' . $s .'">';
		}
		else {
			header('Location: ' . $siteurl);
		}
    */
	}
}
else if (strlen($anchor)) {
		header('Location: ' . $siteurl);
}

// collect the tab data

/*
	Given the data from the LIST JSON request, issued above, this builds the tab data
	by repeated traversals of the result. Note that the devices in this list should
	correspond to all of the known machines in the io daemon. 
	Only machines that have a 'tab' property are displayed.
 */
// build a list of tabs, each with no data initially
$n = 1;
$tabs = array('Outputs' => false, 'Inputs' => false);
foreach ($config_entries as $curr) {
	if (isset($curr->tab) &&  ($curr->tab == 'Modules' || $curr->tab == "Vis3D") ) continue; // the modules tab is added at the end of the list
	if (isset($curr->tab) && !in_array($curr->tab, $tabs)) {
		$tabs[$curr->tab] = false;
		$n++;
	}
}
$messages_tab = $n; $tabs['Messages'] = false;
$tabs['Modules'] = false;
if ($master) $tabs['Master'] = false; // add a master tab if the above MASTER request succeeded
debug_message($current_tab);
// flesh out each tab
$n = 1;
foreach ($tabs as $tab => $data) {
  $tabname = 'tabs-' . $n;
  $tabdata='<div id="'. $tabname . '" name="' . $tab . '" ><table class="data">' . "\n";
  if ($tab != 'Modules' && $tab != 'Messages')
		$tabdata .= '<thead><tr><th style="width:80px">Enabled</th>'
			.	'<th>Name</th><th colspan=2>State</th>'
			.	'<th>Commands</th><th>Messages</th></tr></thead>';
  else if ($tab == 'Messages')
		$tabdata .= '<div>Messages</div>';
  else
		$tabdata .= '<thead><tr><th>Name</th><th>Matched Bus Module</th></tr></thead>';

  if ($tab == "Modules")
	$config_entries = $slaves;
  if ($tab != "Messages") {
	$rownum = 1;
	foreach ($config_entries as $curr) {
		if (!isset($curr->tab)) {
			if ($curr->class == "MODULE")
				$curr->tab = 'Modules';
			else
				continue;
		}
		if ($curr->tab != $tab) continue;
		$row_class = ($rownum % 2 == 1) ? "odd" : "even";
		$tabdata .= "<tr class=\"$row_class\">";
		if ($curr->class != "MODULE" ) { // modules go on their own tab
			$point = $curr->name;
			$image_prefix = "input64x64";
			if (isset($curr->type))
				$type = $curr->type;	
			else if (isset($curr->class))
				$type = $curr->class;	
			else
				$type = "Input";
			if (isset($curr->image))
				$image_prefix = $curr->image; 
			else 
				$image_prefix = $type;
			if (isset($curr->state)) 
				$status = $curr->state;
			else
				$status = "unknown";
			//$debug_messages .= "$point $status <br/>";
			$tabdata .= '<td style="width:80px;"><div syle="width:100%"><div  style="width:20px;margin:0px auto 0px auto"><input type="checkbox"'
					. ( (isset($curr->enabled) && $curr->enabled) ? 'checked="checked"' : '')
					.' name="'. $point . '" class="enable_disable"' 
					.'</div></div></td>';
			$tabdata .= '<td class="item_name" name="'.$point.'">'. $point . ":</td>"; 
			$id = "id=\"mc_$point\"";

			if ($type == "piston") { // pistons have a special layout
			    $tabdata .= "<td><div class=\"piston\" style=\"height:20px; width: 80px;\" $id ></div></td>";
			}
			else {
			    // analogue values display a value, everything else displays a state name
			    if ($type == "AnalogueOutput" || $type == "AnalogueInput")
			        $display_value = htmlspecialchars($curr->value);
			    else
			        $display_value = htmlspecialchars($status);
       
			    // inputs that are on have an input_on icon, analogues all have the same icon otherwise the icon is based on the state name
			    if ($type == "Input" && preg_match("/.*on.*/",$status))
			        $image_name = "${image_prefix}_on.png";
			    else if ($type == "Input" || $type == "AnalogueOutput" || $type == "AnalogueInput")
			            $image_name = "${image_prefix}.png";
			    else 
			        $image_name = "${image_prefix}_$status.png";

			    // if there is no file for the icon, we do not add the image div
			    if (file_exists($BASE_APPDIR . "/html/img/$image_name")) {
				    $tabdata .= "<td>";
			        if ( $type == "AnalogueInput" || $type == "Input")
			            $tabdata .= image_html($point, $image_name, "im_".$point);
			        else
			            $tabdata .= button_image($point, $image_name, "im_". $point);
					$tabdata .= "</td>";
					if ($type == 'AnalogueOutput') {
						$tabdata .= '<td><div class="item_state" id="mc_' . $point. '">' 
							. htmlspecialchars($curr->value) 
							. '</div><div class="anaout-slider" name="' . $point . '" value="' .$curr->value .'"'
							. 'style="float:left;width:14em;"></div></td>';
					}
					else
				    	$tabdata .= "<td><div class=\"item_state\" $id >$display_value</div></td>";
			    }
				else
			    	$tabdata .= "<td colspan=2><div class=\"item_state\" $id >$display_value</div></td>";
			}
			// display command buttons
			if (isset($curr->commands)) {
				$cmds = explode(",", $curr->commands);
				$tabdata .= "<td>";
				foreach ($cmds as $cmd) {
					$tabdata .= '<button class="machine_command" name="' 
						. htmlspecialchars($point .".". $cmd) . '">'. htmlspecialchars($cmd) . '</button>'; 
				}
				$tabdata .= "</td>";
			}
			else $tabdata .= "<td></td>";
			// display properties
			if (isset($curr->display)) {
				$tabdata .= "<td>";
				$hidden = array();
				if (isset($curr->hidden)) {
					$hidden = explode(",",$curr->hidden);
				}
				$props = explode(",", $curr->display);
				foreach ($props as $prop) {
					if ($prop=="hidden" || in_array($prop,$hidden)) continue;
					$tabdata .= '<div name="p_' .htmlspecialchars($point ."-". $prop). '">'
							. htmlspecialchars($prop) .': ';
					if (isset($curr->$prop)) $tabdata .= htmlspecialchars($curr->$prop);
					else $tabdata .= '""';
					$tabdata .= ' </div>';
				}
				$tabdata .= "</td>";
			}
			else
				$tabdata .= "<td></td>";
			// display message buttons
			if (isset($curr->receives)) {
				$cmds = explode(",", $curr->receives);
				$tabdata .= "<td>";
				foreach ($cmds as $cmd) {
					$tabdata .= '<button class="machine_command" name="' . htmlspecialchars($point .".". $cmd) . '">'. htmlspecialchars($cmd) . '</button>'; 
				}
				$tabdata .= "</td>";
			}
			else $tabdata .= "<td></td>";
		}
		else if ($curr->class == "MODULE") {
			// modules display the name and position of the module, along with an option list
			// to select matching devices from the bus.
			if (isset($curr->position) && $curr->position >= 0)
				$slaves_options = get_slaves_options($slaves, $curr->position);
			else
				$slaves_options = get_slaves_options($slaves);
			$tabdata .= "<td>" . $curr->name . " " . $curr->position . "</td><td>" . $slaves_options . "</td>";
		}
		else {
			$tabdata .= "<td>unknown</td>:";
		}
		$tabdata .= "</tr>";
		$rownum++;
	}
 }
else {
	$tabdata .=  "<div id=\"messages\">";
	$tabs['Messages'] = <<<EOD
		<div id="tabs-$messages_tab">
		$tabdata
EOD;
}
	$n++;
 	// end this tab with a 'refresh' button
	$tabdata .= '</table>'
	.'<div><form method=post action=?><input type="hidden" name="anchor" value="'.$tabname.'">'.
	"<button id=refresh>Refresh</button></form></div>"
	.'</div>' . "\n";
	$tabs[$tab] = $tabdata;
}
// The Master tab, if given, has a custom layout
if ($master) {
	$n = 1;
	foreach($tabs as $tabname => $tabdata) { 
		if ($tabname == 'Master') {
			$master_tab = $n; 
			break; 
		}
		$n++;
	}
	$tabs['Master'] = <<<EOD
		<div id="tabs-$master_tab">
		<table><thead><tr><th>Name</th><th>Status</th></tr></thead>\n<tbody>
			<tr><td>Slave Count</td><td>$master->slave_count</td></tr>
			<tr><td>Link Up</td><td>$master->link_up</td></tr>
		</tbody></table>\n</br>
		<div class="userinput">
			<form action="?" method="post">
				<label>EtherCAT Query</label>
				<input name="query" size=60 />
				<input type=submit />
			</form>
		</div>
		<div>
		<pre>$master->statistics</pre>
		</div>
EOD;
}

// display tab headers and data by adding them to the page body.
// first the headers (see jQuery tabs manual)
$page_body .= '<div id="tabs"><ul>';
$n = 1;
foreach ($tabs as $tab => $tabdata) {
	$page_body .= '<li><a href="#tabs-' . $n . '">'. $tab .'</a></li>';
	$n++;
}
$page_body .= "</ul>\n";
// now each of the tab contents
foreach ($tabs as $tabname => $tabdata) $page_body .= $tabdata;
$page_body .= "</div>";

// Page layout. Note from here, no PHP calculation is done, we just render the
// variables we have collected, notably $page_body, $debug_message, $header_extras

print <<<'EOD'
<!DOCTYPE html PUBLIC 
  "-//W3C//DTD HTML 4.01 Transitional//EN" 
  "http://www.w3.org/TR/html4/loose.dtd" >

<html> <head> 
	<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" >
	<script type="text/javascript" src="js/jquery-1.10.2.min.js"></script>
	<script type="text/javascript" src="js/jquery-ui-1.10.3.custom/js/jquery-ui-1.10.3.custom.min.js"></script>
	<link type="text/css" href="js/jquery-ui-1.10.3.custom/css/smoothness/jquery-ui-1.10.3.custom.min.css" rel="stylesheet" />
	<style>
	body { font-family: Helvetica, Arial, sans-serif }
	table { width:100% }
	.error_message { font-size:24px; color:#f44; }
	.center { text-align:center; }
	tr.odd { background-color:#e8e8e8 }
	tr.even { background-color:#ffffff }
	table.data { border-spacing:0px; }
	</style>
	<script type="text/javascript">
	function refresh() {
		var tabs = $('#tabs').tabs();
		var selected = tabs.tabs('option', 'active')+1;
		tabname=$('#tabs-'+selected).attr("name")
		//$("#xx").html("<p>" + tabname + "</p>");
		if (tabname== "Messages") {
			$.get("monitor.php", {"messages": "JSON 50" }, 
				function(data){ 
					res=JSON.parse(data);
					$("#xx").html("<p>AJAX result</p><pre>"+ data+ "</pre>");
					$("#messages").html("");
					for (var i = 0; i < res.length; i++) {
						$("#messages").append("<pre>" + res[i].message + "</pre>");
					}
			})
		}
		else {
			$.get("monitor.php", { 
				list: "json", tab: tabname}, 
				function(data){ 
					res=JSON.parse(data);
					//$("#xx").html("<p>AJAX result</p><pre>"+ data+ "</pre>");
					for (var i = 0; i < res.length; i++) {
						var type = "Input";
						if (typeof res[i].class !== "undefined") type = res[i].class;
						if (typeof res[i].type != "undefined")
							type = res[i].type;
						if (type != "MODULE") {
							btn=$('[name='+res[i].name+']');
							enabled=res[i].enabled;
							btn.each(function() {
								//if ($(this).get(0).tagName == 'checkbox') {
									if (enabled && !$(this).prop("checked")) 
										$(this).prop("checked", true);
									else if (!enabled && $(this).prop("checked"))
										$(this).prop("checked", false);
								//}
							
							});
							//btn=$("[name="+res[i].name+"]");
							btn.each(function() {
								if (typeof res[i].image === "undefined")
									res[i].image = type;
								if (typeof res[i].class === "undefined") {
									$("#mc_"+res[i].name).each(function(){
										$(this).html(res[i].state);
									});
								}
								else if (type == "AnalogueInput") {
									$("#mc_"+res[i].name).each(function(){
										$(this).html(res[i].value);
									});
								}
								else if (type == "AnalogueOutput") {
									$("#mc_"+res[i].name).each(function(){
										$(this).html(res[i].value);
									});
								}
								else if (type != "piston") {
									img= "img/" + res[i].image + "_" + res[i].state + ".png";
								
									$("#im_"+res[i].name).each(function(){
										$(this).attr("src",img);
									});
									$("#mc_"+res[i].name).each(function(){
										$(this).html(res[i].state);
									});
								}
								else {
									var pos = parseInt(res[i].position);
									$("#mc_"+res[i].name).each( function() { $(this).progressbar({ value: pos });  });
									if (typeof res[i].maxpos !== "undefined" )  {
										maxpos = parseInt(res[i].maxpos);
										$("#mc_"+res[i].name).each( function() { $(this).progressbar({ max: maxpos }); });
									}
								}
							});
							btn.select(".item").each(function(){ 
								if (type == "Output") {
									if (res[i].state == "off") {$(this).removeClass("on").addClass("off"); }
									if (res[i].state == "on") {$(this).removeClass("off").addClass("on"); }
								}
							});
							hidden_props = typeof res[i].hidden;
							//TBD
							//var hidden = [];
							//if (hidden_props == "string") {
							//	hidden = res[i].hidden.split(",");
							//}
							display_props = typeof res[i].display;
							if (display_props == "string") {
								props=res[i].display.split(",");
								n = props.length;
								for (var j=0; j<n; j++) {
									attr="[name=p_"+res[i].name+"-"+props[j]+"]";
									prop=$(attr).each(function() {
										$(this).html(props[j] + ":" + res[i][props[j]]);
									});
								}
							}
						}
					}
				}
			);
		}
    	setTimeout("refresh()", 1000);
	}
	$(function() {
		$( "#tabs" ).tabs();
		$( ".anaout-slider" ).slider({
			min: 0,
			max: 32767,
			stop: function( event, ui ) {
				var machine = $(this).attr("name").replace("-",".");
				var slider_value = $(this).slider("option", "value");
				$.get("index.php", { property: machine + " value " + slider_value }, 
					function(data){
						if (data != "OK") alert(data) 
					});
				event.preventDefault();
			}
		});
		$( ".anaout-slider" ).each(function() {
			$(this).slider("option", "value", $(this).attr("value"))
		});
		$("#refresh").click(function(event) {
			//event.preventDefault();
		});
		$(".out").each(function(){
			$(this).click(function(){
				$.get("monitor.php", { toggle: $(this).attr("name") }, 
					function(data){
						if (data != "OK") alert(data) 
					});
			})
		});
		$(".machine_command").each(function(){
			$(this).click(function(){
				$.get("monitor.php", { send: $(this).attr("name") }, 
					function(data){
						$("#xy").html("<p>AJAX result</p><pre>"+ data+ "</pre>");
						//if (data != "OK") alert(data) 
					});
			})
		});
		$( ".piston" ).progressbar({
            value: 0
        });
		$(".enable_disable").each(function() {
			$(this).click(function() {
				if ($(this).prop("checked")) {
					$.get("monitor.php", { enable: $(this).attr("name") },
						function(data) {
							$("#xy").html("<p>AJAX result</p><pre>"+ data+ "</pre>");
						})
				}
				else {
					$.get("monitor.php", { disable: $(this).attr("name") },
					function(data) {
						$("#xy").html("<p>AJAX result</p><pre>"+ data+ "</pre>");
					})
				}
			});
		});
        $("#hideinfo").click( function(){
            $(this).css("display","none");
            $("#info").css("display","none")
        })
        $(".item_name").each(function(){
          $(this).click(function(){
            $.get("index.php", { describe: $(this).attr("name").replace("-",".") },
            function(data){
                $("#info").html("<pre id=\"info-details\">"+data+"</pre>");
				$("#info").css("width",window.innerWidth - 200)
				$("#info").css("height",window.innerHeight - 200)
				$("#hideinfo").css("left", 150)
                $("#info").css("display","block");
                $("#hideinfo").css("display","block");
			    //$("#info").click( function(){
			    //    $(this).css("display","none"); 
			    //    $("#info").css("display","none")
	            //	$("#hideinfo").css("display","none");
			    //})  
            })
          })
        })

		setTimeout("refresh()", 900);
	})
	</script>
EOD;
print <<<EOD
	$header_extras 
</head>
<body>
<div>$banner</div>
<fieldset><legend>IO Control</legend>
<div style="position:relative;background-color:white">
  <div id="hideinfo" style="height:18px;width:64px;text-align:center;font-size:16px;position:absolute;left:600px;top:80px;display:none;z-index:1000;background-color:yellow;">hide</div>
  <div id="info" style="border:1px solid black;position:absolute;left:100px;top:100px;width:800px;height:500px;background-color:white;overflow:scroll;display:none;z-index:1000;" >
  </div>
</div>
$page_body
<br/>
</fieldset>
<br/>
<div><form method=post action=?><input type="hidden" name="quit" value="true">
<button id=quit>Quit</button></form></div>
<div>
<fieldset><legend>Debug Messages</legend>
<pre>
$debug_messages
</pre>
<pre id="xy"></pre>
<pre id="xx"></pre>
</fieldset>
</div>
</body></html>
EOD;
}
