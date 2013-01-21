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
function image_html($name, $filename) {
	return '<img width=16 name="'.$name.'" src="img/' .  $filename . '"/>';
}

// toggle button html generator
function button_image($name, $filename, $item_id = false) {
	global $use_ajax;
	$id = "";
	if ($item_id) $id = "id=\"${item_id}\"";
	if ($use_ajax)
		return '<img width=16 class="out" '.$id. ' width=16 name="'.$name.'" src="img/' .  $filename . '"/>'. "\n";
	else
		return '<input type="image" width=16 '.$id .' name="'.$name.'" src="img/' .  $filename . '"/>';
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
// note: below is ignored if an AJAX request was received

$siteurl="index.php";
//header('Location: ' . $siteurl);

// collect the tab data

/*
	Given the data from the LIST JSON request, issued above, this builds the status 
	display by a traversals of the result. Note that the devices in this list should
	correspond to all of the known machines in the io daemon. 
	Only machines that have a 'tab' property are displayed.
 */
  $tabdata="";

  foreach ($config_entries as $curr) {
	if ($curr->class != "MODULE" && (isset($curr->wire) || isset($curr->monitored) && $curr->monitored == "true") ) { 
        $tabdata .= "<div class=\"item\">";
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
		//$debug_messages .= "$point $status <br/>";
		if ($type != "Input") { 
			// interactive objects
			if ($use_ajax) {
				if ($type != "piston") {
					$tabdata .= '<div class="item_name">' . $point . $wire . "</div> "
						. '<div class="item_img">' 
						. button_image($point, "{$image_prefix}_$status.png", 'im_'.$point) . "</div>"
						. '<div class="item_state" id="mc_' . $point. '">' 
						. htmlspecialchars($status) . "</div>";
				}
				else {
					$tabdata .=  $point . '<div class="piston" style="height:20px; width: 80px;"  id="mc_'.$point.'"></div>';
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
			$tabdata .=  $point . $wire . ":";
			// TBD rather than a match, use the status property in the response
			if (preg_match("/.*on.*/",$status))
				 $tabdata .= image_html($point, $image_prefix . "_on.png");
			else
				$tabdata .= image_html($point, $image_prefix . ".png");
			$tabdata .= "\n";
		}
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

print <<<EOD
<!DOCTYPE html PUBLIC 
  "-//W3C//DTD HTML 4.01 Transitional//EN" 
  "http://www.w3.org/TR/html4/loose.dtd" >

<html> <head> 
	<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" >
	<script type="text/javascript" src="js/jquery-1.7.min.js"></script>
	<script type="text/javascript" src="js/jquery-ui-1.8.20.custom.min.js"></script>
    <link type="text/css" href="css/ui-lightness/jquery-ui-1.8.20.custom.css" rel="stylesheet" />   
    <style>
		body { font-family: Helvetica, Arial, sans-serif }
	    .error_message { font-size:24px; color:#f44; }
	    .item { float:left; width:240px; }
	    .debug_messages { display:none; }
		.item_name { width:120px; height:40px;font-weight:bold;float:left; }
		.item_img { width:20px; height:40px;float:left; }
		.item_state { width:80px; padding-left:5px; height:40px;float:left; }
    </style>
	<script type="text/javascript">
	function refresh() {
		$.get("index.php", { list: "json"}, 
			function(data){ 
				res=JSON.parse(data);
				$("#xx").html("<p>AJAX result</p><pre>"+ data+ "</pre>");
				for (var i = 0; i < res.length; i++) {
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
							if (enabled && !$(this).attr("checked")) 
								$(this).attr("checked", true);
							else if (!enabled && $(this).attr("checked"))
								$(this).attr("checked", false);
							
						});
						btn=$("[name="+res[i].name+"]");
						btn.each(function() {
							if (typeof res[i].image === "undefined") res[i].image = res[i].class;
							img= "img/" + res[i].image + "_" + res[i].state + ".png";
							if ($(this).attr("src") != img) $(this).attr("src",img);
							if (typeof res[i].type === "undefined" || res[i].type != "piston") {
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
		$("#refresh").click(function(event) {
			//event.preventDefault();
		});
		$(".out").each(function(){
			$(this).click(function(){
				$.get("index.php", { toggle: $(this).attr("name") }, 
					function(data){
						if (data != "OK") alert(data) 
					});
			})
		});
		$( ".piston" ).progressbar({
            value: 0
        });
		setTimeout("refresh()", 2000);
	})
	</script>
	$header_extras 
</head>
<body>
<div>$banner</div>
<fieldset id="error_messages" style="display:none"><legend>Error</legend>
	<div class="error_message"></div>
</fieldset>
<fieldset><legend>Status</legend>
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
