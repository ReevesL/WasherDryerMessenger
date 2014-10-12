<?php

 function Conection(){
$DB_host = 'YOUR_SQL_HOST_HERE'; // mysql host 
$DB_login = "YOUR_ID_HERE"; // mysql login 
$DB_password = "YOUR_PW_HERE"; // mysql password 
$DB_database = "YOUR_DBNAME_HERE"; // the database which can be used by the script. 

    if (!($link=mysql_connect($DB_host,$DB_login,$DB_password)))  {
echo "Failed to connect to MySQL: ";
       exit();
    }
    if (!mysql_select_db($DB_database,$link)){
echo "Failed to open table: ";     
  exit();
    }
    return $link;
 }
 ?>
