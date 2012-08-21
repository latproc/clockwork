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

<?php
/*
 *  iod client
 *  sends requests to the server to get the status of each point,
 */

$debug_messages = "";

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
$requester->send('LIST JSON');
$reply = $requester->recv();
$config_entries_json = $reply;
$config_entries = json_decode($reply);
$res = json_last_error();
if (json_last_error() != JSON_ERROR_NONE) {
	display_json_error($res, $reply);
	$config_entries = array();
}
$debug_messages .= var_export($config_entries, true);
$props = array("type","event", "group", "msg", "id");
print "name";
foreach ($props as $prop) {
	print "," . $prop;
}
print("\n");
  foreach ($config_entries as $curr) {
	print $curr->name ;
	foreach ($props as $prop) {
		if (isset($curr->$prop)) 
			print "," . $curr->$prop;
		else print ",";
	}
    print "\n";
  }
