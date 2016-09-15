<?php session_start();

function myexception_handler(Exception $ex) {
    echo "Uncaught exception: ". $ex->getMessage() . "\n";
}
set_exception_handler('myexception_handler');

$BASE_APPDIR="/opt/latproc/www";
$APPDIR = $BASE_APPDIR . "/app";
date_default_timezone_set("Australia/Adelaide");

set_include_path(get_include_path() . ":" . $APPDIR );
$DBNAME=$APPDIR . '/webio.db';
$APP_ERRORS = Array();
function __autoload($class_name) { 
      $BASE_APPDIR="/opt/latproc/www";
      $APPDIR = $BASE_APPDIR . "/app";
	  date_default_timezone_set("Australia/Adelaide");
      
      switch($class_name) {
          case 'Model':
		case 'IOModule':
		case 'IOSyncManager':
		case 'IOPoint':
            case 'User':
                set_include_path(get_include_path() . ":" . $APPDIR );
                require_once(strtolower($class_name) . '.php'); 
          
          default:
              set_include_path(get_include_path() . ":" . $APPDIR );
              require_once(strtolower($class_name) . '.php'); 
      }
   }
if ( isset($_SESSION['user']) ) $user = $_SESSION['user']; 
error_reporting(E_ALL);
$APP_DEBUG_MESSAGES = Array();
$DEBUG_MESSAGES_ENABLED = true;
function debug_message($msg) {
    global $APP_DEBUG_MESSAGES; 
    $APP_DEBUG_MESSAGES[] = $msg;
}

?>
