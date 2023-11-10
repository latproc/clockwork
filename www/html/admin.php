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

$page_body = '';
if ( (isset($_REQUEST['m']) && $_REQUEST['m'] == 'login')) {
   
       if (isset($user)) unset($user);
       $username = isset($_REQUEST['username']) ? $_REQUEST['username'] : '';
       $password =  isset($_REQUEST['password']) ? $_REQUEST['password'] : '';
       if ($username != '' && $password != '') {
           $username = $_REQUEST['username'];
           $password = $_REQUEST['password'];
           
               $message = "Not logged in";
               try {
                   $user = new User($username, $password);
                   $_SESSION['user'] = $user;
                   unset($message);
                   header('Location: admin.php'); // redirect to strip the login parameters
               } catch (Exception $e) {
                   $message = $e->getMessage();
               }
           
       }
       else {
           $message = "Insufficient credentials to login. Please try again.";
       }
       if (isset($message)) array_push($APP_ERRORS, $message);
   
}
else if ( (isset($_REQUEST['m']) && $_REQUEST['m'] == 'logout')) {
   if (isset($user)) {
     unset($user);
     unset($_SESSION['user']);
     header('Location: admin.php'); // redirect to strip the login parameters
     return;
   }
   
}
else if ( (User::count() == 0 || (isset($user) && $user->isAdministrator())) 
		&& isset($_REQUEST['m']) && $_REQUEST['m'] == 'register') {
   
       if (isset($_REQUEST['username']) && isset($_REQUEST['password'])
        && isset($_REQUEST['confirm'])) {
         $username = $_REQUEST['username'];
         $password = $_REQUEST['password'];
         $confirm = $_REQUEST['confirm'];
		 $admin = isset($_REQUEST['administrator']);
         $email = isset($_REQUEST['email']) ? $_REQUEST['email'] : '';
         try {
           if (strlen($username) < 2 ) 
               throw new exception("username must be at least 2 characters");
           if (strlen($password) < 6 ) 
               throw new exception("password must be at least 6 characters");
           if (!preg_match("/^[a-zA-Z_@.]+$/",$username)) 
               throw new Exception("username may only contain a-z,'@','.' and '_'");
           if ($password != $confirm) throw new Exception("password and confirmation did not match");
           $message = "";
   
           $dbcon = new PDO('sqlite:'.$DBNAME);
           // test for existing user
           $sql = "select id,username,administrator from users where username=" 
               . $dbcon->quote($username);
           $query = $dbcon->query($sql);
           if ($query) $row = $query->fetch();
           else throw new exception("query failed when checking if the user exists: ". $sql);
           if ($row !== FALSE) throw new exception("that username has been taken");
               $sql = "select count(*) as count from users;";
               $query = $dbcon->query($sql);
               if ($query) $row = $query->fetch();
               else throw new exception("query failed when getting new user id: ". $sql);
               $sql = "insert into users (username, encrypted_password, administrator, email) "
                     . " values(" . $dbcon->quote($username) 
                     . ",'" . md5($password) . "', " . (($admin) ? '1' : '0') . "," 
                     . $dbcon->quote($email) . ");";
           
           if ($dbcon->exec($sql) === false) 
               throw new Exception("failed to add new user, please try a different name. " . $sql);
           else {
               $dbcon = null;
           }
         } catch (Exception $e) {
            $message = $e->getMessage();
         }
       }
       else {
           $message = "Insufficient credentials to register. Please try again."
               . " " . implode("<br>", $_REQUEST);
           
       }
       if (isset($message)) array_push($APP_ERRORS, $message);
   
   
}

if (User::count() == 0 || isset($user)) {
   if ( (User::count() == 0) || ($user != null && array_key_exists('registration', $_REQUEST))) {
       
       if (!isset($username)) $username='';
       if (!isset($email)) $email='';
       $registration_form = <<<EOD
       <h1>Create a new account</h1>
       <p>To create a new account, please enter the registration details, below.<p>
       <form id="registration_form" action='admin.php?m=register' method=post>
          <p>username:<input type="text" name="username" default=$username ></p>
          <p>password:<input type="password" name="password"></p>
          
             <p>confirm password:<input type="password" name="confirm"></p>
             <p>email:<input type="text" name="email" default=$email > (optional)</p>
			<p><input type="checkbox" name="administrator">Administrator</input>
          
          <p><input type="submit" value="Register"></p>
       </form>
EOD;
       
       $page_body = $registration_form;
   }
   else {
       
           try {
               
				header('Location: monitor.php');
               $banner = "<p>Logged in as " . $user->getName() . "."
                   . " <a href=\"admin.php?m=logout\">Logout</a>";
				if ($user->isAdministrator()) {
                   $banner .= ' <a href="admin.php?registration">Administration</a>';
				}
               
               $user_profile = '<p>to be completed</p>'; 
               
               $page_body = <<<EOD
                   $banner


EOD;
           } catch (Exception $e) {
               $message = $e->getMessage();
               $banner = "<h1>Error</h1>";
               $page_body = <<<EOD
                   $banner
                   
                   <h1>Website Problem</h1>
                   <p>An error has occured:<br>
                   $message
                   </p>
EOD;
           }
       
   }
}
else {
   
   if (!isset($username)) $username='';
   $login_form = <<<EOD
   <h1>Login</h1>
   <p>To access the system, please login<p>
   <form id="login_form" action='admin.php?m=login' method=post>
      <p>username:<input type="text" name="username" default=$username ></p>
      <p>password:<input type="password" name="password"></p>
      <p><input type="submit" value="Login"></p>
   </form>
EOD;
   
   
   $page_body = $login_form;
}
if (count($APP_ERRORS) > 0)
    $error_messages = '<p class="errors">' . implode('<br/>',$APP_ERRORS) . '</p>';
else
    $error_messages = '';


if ($DEBUG_MESSAGES_ENABLED) {
    $debug_messages = "<h1>Debug Messages</h1>\n";
    if (count($APP_DEBUG_MESSAGES) > 0)
        $debug_messages .= '<p class="debug_message">' . implode('<br/>',$APP_DEBUG_MESSAGES) . '</p>';
}
else $debug_messages = '';


print <<<EOD
<!DOCTYPE html PUBLIC 
  "-//W3C//DTD HTML 4.01 Transitional//EN" 
  "http://www.w3.org/TR/html4/loose.dtd" >

<html> <head> 
  <meta http-equiv="Content-Type" content="text/html; charset=UTF-8" > 
  
	<script type="text/javascript" src="js/jquery-1.10.2.min.js"></script>
	<script type="text/javascript" src="js/jquery-ui-1.10.3.custom/js/jquery-ui-1.10.3.custom.min.js"></script>
	<link type="text/css" href="js/jquery-ui-1.10.3.custom/css/smoothness/jquery-ui-1.10.3.custom.min.css" rel="stylesheet" />

  <script>
     $(function() {
        $('.datepicker').each(function(){ 
        $(this).datepicker({
            dateFormat: 'dd-mm-yy', defaultDate: $(this).val() 
        } ); });
     });
  </script>
  
  
  <style> 
  .formContainer {
    width:600px; height:600px;
  } 
  .number { 
    text-align:right; 
  }
  table { width:100%; }
  th, td {text-align: left; }
  </style>
  <title>Clockwork WebIO Controller</title>
</head>
<body>
$error_messages $page_body $debug_messages 
</body></html>
EOD;

