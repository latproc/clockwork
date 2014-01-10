<?php require_once('settings.php');
  
class User {
  private $administrator;
  private $id;
  private $username;

  public function __construct($username, $password) {
    global $DBNAME;
	$dbcon = new PDO('sqlite:'.$DBNAME);
    $sql = "select id,username,administrator from users where username="
      . $dbcon->quote($username) . " and encrypted_password='" . md5($password). "'";

    $row = $dbcon->query($sql)->fetch();
    if ($row === FALSE) { 
      throw new Exception("invalid user/password combination");
    }
    $this->administrator = $row['administrator'];
    $this->id= $row['id'];
    $this->username = $row['username'];
  }
  public function isAdministrator() { return $this->administrator == 1; }
  public function getName() { return $this->username; }
  public function getId() { return $this->id; }
  public static function count() { 
    global $DBNAME;
    $dbcon = new PDO('sqlite:'.$DBNAME);
    if ($dbcon == false) 
	throw new Exception("error connecting to the database " . $DBNAME);
    $sql = 'select count(*) from users;';
    $res = $dbcon->query($sql);
    if ($res === false) { 
      throw new Exception("error counting users in " . $DBNAME);
    }
    $row = $res->fetch();
    return $row[0];
  }
}
?>
