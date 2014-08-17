<?php include_once('settings.php');

define("REQUEST_TIMEOUT", 100);
define("DSN", "tcp://localhost:5555");

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
	return 'Error talking to clockwork';
}


function loadPage($file_name, $replacements = array('key'=>false, 'html'=>false)) {
	$form_html = file_get_contents($file_name);
	if ($form_html === false) return false;
	$placeholder = '<div class="validation-placeholder"></div>';
	if ($replacements['keys'])
		$form_html = str_replace($replacements['keys'], $replacements['html'], $form_html);
	return $form_html;	
}

function addReplacement(&$replacements, $key, $value) {
	if (!$replacements) $replacements = array('key'=>array(), 'html'=>array());
	$replacements['keys'][] = $key;
	$replacements['html'][] = $value;
}

$context = new ZMQContext();
$requester = new ZMQSocket($context, ZMQ::SOCKET_REQ);
$requester->setSockOpt(ZMQ::SOCKOPT_LINGER, 5);
$requester->connect('tcp://localhost:5555');
$requester->setSockOpt(ZMQ::SOCKOPT_BACKLOG, 0);

if (isset($_REQUEST["list"])) {
	if (isset($_REQUEST["tab"]))
		print zmqrequest($requester,'LIST JSON' . ' ' . $_REQUEST['tab']);
	else
		print zmqrequest($requester,'LIST JSON');
	exit;
}
else if (isset($_REQUEST["command"]) ) {
	print zmqrequest($requester, 'SEND ' . $_REQUEST['name'] . '.' . $_REQUEST['command']);
//	print "test";
	exit;
}
else if (isset($_REQUEST["save_layout"])) {
	if (!file_put_contents("/tmp/layout.json", $_REQUEST['layout']))
		print("Error saving layout");
	exit;
}
else if (isset($_REQUEST["load_layout"])) {
	$str = file_get_contents("/tmp/layout.json");
	if (!$str) { print("Error loading layout"); return; }
	print $str;
	exit;
}


$title = "Operator's Console";
$repl = null;
addReplacement($repl, '%title%', $title);
$page = loadPage('templates/vis.php.inc', $repl);
print $page;
return;