<?php require_once('settings.php');

define("REQUEST_TIMEOUT", 100);
define("DSN", "tcp://localhost:5555");


$title = "Runtime performance";

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
	return;
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
function zmqrequest($client, $request) {
	$read = $write = array();
	$client->send($request);
	$poll = new ZMQPoll();
	$poll->add($client, ZMQ::POLL_IN);
	$events = $poll->poll($read, $write, REQUEST_TIMEOUT);
	if ($events > 0) {
		$result = $client->recv();
		return $result;
	}
	return false;
}

$debug_messages = "";
$header_extras='';
$page_body='';

$context = new ZMQContext();
$requester = new ZMQSocket($context, ZMQ::SOCKET_REQ);
$requester->setSockOpt(ZMQ::SOCKOPT_LINGER, 5);
$requester->connect('tcp://localhost:5555');
$requester->setSockOpt(ZMQ::SOCKOPT_BACKLOG, 0);

$device = "localhost";
if (isset($_REQUEST['stats'])) {
	$reply = zmqrequest($requester,'STATS JSON');
	$entries = json_decode($reply);
	$res = json_last_error();
	if (json_last_error() != JSON_ERROR_NONE) {
		display_json_error($res, $reply);
		$entries = array();
	}
	else {
		echo json_encode($entries);
		return;
	}
}

if (isset($_REQUEST["download"])) {
	$reply = zmqrequest($requester,'MODBUS REFRESH');
	$config_entries_json = $reply;
	$config_entries = json_decode($reply);
	$res = json_last_error();
	if (json_last_error() != JSON_ERROR_NONE) {
		display_json_error($res, $reply);
		$config_entries = array();
	}

	header( 'Content-Type: text/csv' );
	header( 'Content-Disposition: attachment;filename="stats.csv"');
	$fp = fopen('php://output', 'w');
	fputcsv($fp,array('name,count'));
	
	foreach ($config_entries as $curr) {
//		fputcsv($fp, modbusToPanel($device, $curr));
	}
	fclose($fp);
	return;
}

$banner = "<p>Logged in as " . $user->getName() . ". <a href=\"admin.php?m=logout\">Logout</a>";
if ($user->isAdministrator()) {
	$banner .= '&nbsp;&nbsp;<a href="admin.php?registration">Administration</a>';
}
$banner .= "<p>";

print <<<EOD
<html>
<head>
	<title>$title</title>
	<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" >
	<script type="text/javascript" src="js/jquery-1.10.2.min.js"></script>
	<script type="text/javascript" src="js/jquery-ui-1.10.3.custom/js/jquery-ui-1.10.3.custom.min.js"></script>
	<link type="text/css" href="js/jquery-ui-1.10.3.custom/css/smoothness/jquery-ui-1.10.3.custom.min.css" rel="stylesheet" />
	<script src="http://d3js.org/d3.v3.min.js" charset="utf-8"></script>
	<style>
	body { font-family: Helvetica, Arial, sans-serif }
	canvas { width: 100%; height: 100% }
	table { width:100% }
	.error_message { font-size:24px; color:#f44; }
	.center { text-align:center; }
	.info {z-index: 100; color:white; position: absolute; top: 10px; width: 100%; display:block;}
	.lhs { float:left;width:1000px }
	.rhs { width:500px }
	</style>
	<script type="text/Javascript">
		function setData(id, dataset) {
			d3.select(id)
			    .append("table")
			    .style("border-collapse", "collapse")
			    .style("border", "2px black solid")

			    .selectAll("tr")
			    .data(dataset)
			    .enter().append("tr")

			    .selectAll("td")
			    .data(function(d){return d;})
			    .enter().append("td")
			    .style("border", "1px black solid")
			    .style("padding", "10px")
			    .on("mouseover", function(){d3.select(this).style("background-color", "aliceblue")}) 
			    .on("mouseout", function(){d3.select(this).style("background-color", "white")}) 
			    .text(function(d){return d;})
			    .style("font-size", "12px");

		}
		function test() { 
			$.get("stats.php", { stats: "" }, 
			function(data){
				//if (data != "OK") alert(data);
				var dataset = JSON.parse(data);
				$("#details").html("");
				setData("#details",dataset);
/*
				var i = 0;
				while (i < dataset.length) {
					//if (i==0) alert(dataset[i].stats[0]);
					for (var j=0; j<dataset[j].stats.length; j++)
					{
						//setData("#details", dataset[i].stats[j]);
						$("#details").append("<div>"+dataset[i].name+","+dataset[i].stats[j]+"</div>");
					}
					i++;
				}
*/
			});
			/*
				$.get("stats.php", { list: "JSON" }, 
				function(data){
					if (data != "OK") alert(data);
					var res = JSON.parse(data);
					var i = 0;
					while (i < res.length) {
						if (typeof(res[i].export) == "undefined")
							res.splice(i,1);
						else
							i++;
					}
					setData("#details", res);
				});
			*/
			event.preventDefault();
		}
		$(function(){
			test(); 
		});
	</script>
</head>
<body>
$banner
<div id="details" class="lhs"></div>
</body>
</html>
EOD;
