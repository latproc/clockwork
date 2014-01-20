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
  along with Latproc; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/* System monitor to display the status of all IO points and
 * error messages.
 */


$debug_messages = "";
$use_ajax = true; // the 'false' version of this may not actually work

$banner = "";

// static image generator
function image_html($name, $filename, $item_id = false) {
	if ($item_id) $id = "id=\"${item_id}\"";
	else $id = "";
	return '<img width=20 name="'.$name.'" '.$id.' src="img/' .  $filename . '"/>';
}

// toggle button html generator
function button_image($name, $filename, $item_id = false) {
	global $use_ajax;
	$id = "";
	if ($item_id) $id = "id=\"${item_id}\"";
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

//             Program starts here 

//         PART I - collect data

$header_extras='';
$page_body='';

$context = new ZMQContext();
$requester = new ZMQSocket($context, ZMQ::SOCKET_REQ);
$requester->setSockOpt(ZMQ::SOCKOPT_LINGER, 0);
$requester->setSockOpt(ZMQ::SOCKOPT_BACKLOG, 1);
$requester->connect("tcp://localhost:5555");

/* 
	retrieve a list of the points to define the initial layout;after this, the 
	javascript updates the status but the layout is not changed unless a refresh
	is done.
 */

$current_tab = "";
if (isset($_REQUEST["tab"]))
	$current_tab = " " . $_REQUEST["tab"];
$requester->send('LIST JSON' . $current_tab);
$reply = $requester->recv();
$config_entries_json = $reply;
$config_entries = json_decode($reply);
$res = json_last_error();
if (json_last_error() != JSON_ERROR_NONE) {
	display_json_error($res, $reply);
	$config_entries = array();
}
//$debug_messages .= var_export($config_entries, true);

$debug_messages .= var_export($_REQUEST, true);
/*
	AJAX handlers for a status update and other commands
 */

if (isset($_REQUEST["list"])) {
	echo $config_entries_json;
	return;
}

if (isset($_REQUEST["quit"])) {
	$requester->send('QUIT');
	$reply = $requester->recv();
	$debug_messages = $reply;
}
if (isset($_REQUEST["describe"])) {
	$requester->send('DESCRIBE ' . $_REQUEST["describe"] . " JSON");
	$reply = $requester->recv();
	echo $reply;
	return;
}

/*
	Handle a user-click on a toggle button for an output
 */

if (isset($_REQUEST["toggle"])) {
	$point = $_REQUEST["toggle"];
	$requester->send('TOGGLE ' . $point);
	$reply = $requester->recv();
	$debug_messages .= "TOGGLE " . $point . ": " . $reply . "\n";
	usleep(100000);
	echo "$reply"; // ajax version
	return; // ajax version
}

if (isset($_REQUEST["property"])) {
	$point = $_REQUEST["property"];
	$requester->send('PROPERTY ' . $point);
	$reply = $requester->recv();
	$debug_messages .= "PROPERTY " . $point . ": " . $reply . "\n";
	usleep(100000);
	echo "$reply"; // ajax version
	return; // ajax version
}

// note: below is ignored if an AJAX request was received

$siteurl="status-monitor.php";
//header('Location: ' . $siteurl);

// collect the tab data

/*
	Given the data from the LIST JSON request, issued above, this builds the status 
	display by a traversals of the result. Note that the devices in this list should
	correspond to all of the known machines in the io daemon. 
	Only machines that have a 'tab' property are displayed.
 */
  $tabdata="";
  $last_module = -1;
  $module_pos = 0;
  $tabdata .= '<div class="module rolledup">';
  foreach ($config_entries as $curr) {
	if ($curr->class != "MODULE"){ //  && (isset($curr->wire) || isset($curr->monitored) && $curr->monitored == "true") ) { 
		if (isset($curr->module))
			$module_pos = $curr->module;
		else
			$module_pos = 0;
		if ($last_module != -1 && $module_pos != $last_module) {
			$tabdata .= '</div>';
			$tabdata .= '<div class="module rolledup">';
		}
		if ($module_pos != $last_module) {
			if (isset($curr->module_name))
				$module_name = $curr->module_name;
			else 
				$module_name = "Clockwork machines";
			$parts = split(" ", $module_name);
			$tabdata .= '<h2 style="margin: 1em 0em 0em 0em;">Module '.$module_pos.' '.$parts[0].'</h2>';
			$tabdata .= "<div class=\"module-name\">$module_name</div>";
		}
		$tabdata .= "\n";

		$point = $curr->name;
		$image_prefix = "input64x64";
		if (isset($curr->image))
			$image_prefix = $curr->image; 
		else if (isset($curr->type)) {
			if ($curr->type != "Input")
				$image_prefix = $curr->type;
		}
		else
			$image_prefix = $curr->class;
		if (isset($curr->type))
			$type = $curr->type;	
		else if (isset($curr->class))
			$type = $curr->class;	
		else
			$type = "Input";
		if (isset($curr->state)) 
			$status = $curr->state;
		else
			$status = "unknown";

		$wire = "";
		if (isset($curr->wire)) $wire = " (" . $curr->wire . ")";
		if ($type == "Output" && preg_match("/.*on.*/",$status))
			$fmt_class = "on";
		else
			$fmt_class = "off";

        $tabdata .= "<div class=\"item $fmt_class\"". 'name="'.$point.'">';
		//$debug_messages .= "$point $status <br/>";
		if ($type != "Input" && $type != "AnalogueInput") { 
			// interactive objects
			if ($use_ajax) {
				if ($type != "piston") {
					if ($type == "Output") $cls_extra = "out"; else $cls_extra = "";
					$tabdata .= '<div class="itemname ' . $cls_extra
						. '" name="'.$point.'">' . $point . "</div> ";
					if ($type == "AnalogueOutput")
						$image_name = "{$image_prefix}.png";
					else
						$image_name = "{$image_prefix}_$status.png";
					if (file_exists($BASE_APPDIR . "/html/img/$image_name"))
						$tabdata .= '<div class="item_img">' . button_image($point, $image_name, 'im_'.$point) . "</div>";
					if ($type == 'AnalogueOutput') {
						$tabdata .= '<div class="item_state" id="mc_' . $point. '">' 
							. htmlspecialchars($curr->value) 
							. '</div><div class="anaout-slider" name="' . $point . '" value="' .$curr->value .'"'
							. 'style="float:left;width:14em;"></div>';
					}
					else {
						$tabdata .= '<div class="item_state" id="mc_' . $point. '">' 
							. htmlspecialchars($status) . "</div>";
					}
				}
				else {
					$tabdata .=  '<div class="itemname" name="'.$point.'">' . $point 
						. '</div><div class="piston" style="height:20px; width: 80px;"  id="mc_'.$point.'"></div>';
				}
				// display properties
				//if (isset($curr->display)) {
				//	$props = explode(",", $curr->display);
				//	foreach ($props as $prop) {
				//		$tabdata .= '<div name="p_' .htmlspecialchars($point ."-". $prop). '">'
				//				. htmlspecialchars($prop) .': ';
				//		if (isset($curr->$prop)) $tabdata .= htmlspecialchars($curr->$prop);
				//		else $tabdata .= '""';
				//		$tabdata .= ' </div>';
				//	}
				//}
			}
		}
		else { 
			// static objects
			$tabdata .=  "<div class=\"itemname\">$point:";
			// TBD rather than a match, use the status property in the response
			if ($type != "AnalogueInput" && preg_match("/.*on.*/",$status)) 
				$image_name = $image_prefix."_on.png";
			else
				$image_name = $image_prefix.".png";
			if ($type != "AnalogueInput")
				$display_value = htmlspecialchars($status);
			else
				$display_value = $curr->value;
			if (file_exists($BASE_APPDIR . "/html/img/$image_name"))
				$tabdata .= image_html($point, $image_name, "im_".$point);
			$tabdata .= "</div>"
					. '<div class="item_state" id="mc_' . $point. '">' 
					. "$display_value</div>\n";
		}
		
		$last_module = $module_pos;
	}
    $tabdata .= '</div>';
  }
  // end this tab with a 'refresh' button

// display tab headers and data by adding them to the page body.
// first the headers (see jQuery tabs manual)
// now each of the tab contents
$page_body .= $tabdata;

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
	    .error_message { font-size:24px; color:#f44; }
	    .item { margin:2px;float:left; width:290px; }
	    .debug_messages { display:none; }
		.itemname { margin-top:10px;width:150px; height:40px;font-weight:bold;float:left; }
		.item_img { margin-top:10px; width:20px; height:40px;float:left; }
		.item_state { margin-top:10px; width:80px; padding-left:5px; height:40px;float:left; }
		.module-name { font-size:80%; font-style:italic }
		.on { background-color:#0c0; }
		.off { background-color:#fff; }
		.rolledup{ height:68px; overflow-y:hidden }
		.module { float:left;width:300px; }
    </style>
	<script type="text/javascript">
	function refresh() {
		$.get("index.php", { list: "json"}, 
			function(data){ 
				res=JSON.parse(data);
				$("#xx").html("<p>AJAX result</p><pre>"+ data+ "</pre>");
				var $last_module = -1;
				for (var i = 0; i < res.length; i++) {
					var type = "Input";
					if (typeof res[i].class !== "undefined") type = res[i].class;
					if (typeof res[i].type != "undefined")
						type = res[i].type;
					if (res[i].class != "MODULE") {
						if (typeof res[i].Cause != 'undefined') {
							if (res[i].Cause != "")  {
								$(".error_message").html(res[i].Cause);
								$("#error_messages").show();
							}
							else 
								$("#error_messages").hide();
						}
						btn=$('[name='+res[i].name+']');
						enabled=res[i].enabled;
						btn.each(function() {
							//if ($(this).get(0).tagName == 'checkbox') {
								if (enabled && !$(this).attr("checked")) 
									$(this).attr("checked", true);
								else if (!enabled && $(this).attr("checked"))
									$(this).attr("checked", false);
							//}
							
						});
						btn=$("[name="+res[i].name+"]");
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
				$.get("index.php", { property: machine + " " + slider_value }, 
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
			$(this).click(function(event){
				$.get("index.php", { toggle: $(this).attr("name").replace("-",".") }, 
					function(data){
						if (data != "OK") alert(data) 
					});
				event.preventDefault();
			})
		});
		$( ".piston" ).progressbar({
            value: 0
        });
	    $("#hideinfo").click( function(){
	        $(this).css("display","none"); 
	        $("#info").css("display","none")
	    })  
		$(".itemnamex").each(function(){
		  $(this).click(function(){
    		$.get("index.php", { describe: $(this).attr("name").replace("-",".") },
        	function(data){
            	$("#info").html('<pre id="info-details">'+data+"</pre>");
            	$("#info").css("display","block");
            	$("#hideinfo").css("display","block");
			    $("#info").click( function(){
			        $(this).css("display","none"); 
			        $("#info").css("display","none")
	            	$("#hideinfo").css("display","none");
			    })  

        	})  
		  })
     	})
     	$(".module").each(function(){
			$(this).click(function(e){
				var parentOffset = $(this).offset();
				var relY = e.pageY - parentOffset.top;
				if (relY < 80) $(this).toggleClass("rolledup");
				event.preventDefault();
			})
		})	  
		setTimeout("refresh()", 2000);
	})
	</script>
EOD;
print <<<EOD
	$header_extras 
</head>
<body>
<div>$banner</div>
<fieldset id="error_messages" style="display:none"><legend>Error</legend>
	<div class="error_message"></div>
</fieldset>
<fieldset><legend>Status</legend>
<div style="position:relative">
  <div id="hideinfo" style="position:absolute;left:600px;top:80px;display:none;z-index:100;background-color:yellow">hide</div>
  <div id="info" style="position:absolute;left:100px;top:100px;width:600px;height:400px;overflow:scroll;display:none;z-index:100;background-color:white" >
  </div>
</div>
$page_body
<br/>
</fieldset>
<br/>
<fieldset class="debug_messages"><legend>Debug Messages</legend>
<pre>
$debug_messages
</pre>
<pre id="xx"></pre>
</fieldset>
</div>
</body></html>
EOD;
