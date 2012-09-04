<?php session_start();
$BASE_APPDIR="/opt/latproc/www";
$APPDIR = $BASE_APPDIR . "/app";

set_include_path(get_include_path() . ":" . $APPDIR );
$DBNAME=$APPDIR . '/webio.db';
$APP_ERRORS = Array();
function __autoload($class_name) { 
      $BASE_APPDIR="/opt/latproc/www";
      $APPDIR = $BASE_APPDIR . "/app";
      
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
$APP_DEBUG_MESSAGES = Array();
$DEBUG_MESSAGES_ENABLED = false;
function debug_message($msg) {
    global $APP_DEBUG_MESSAGES; 
    $APP_DEBUG_MESSAGES[] = $msg;
}

?>
