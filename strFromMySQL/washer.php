<?php
    include("conec.php");
    $link=Conection();
    $result=mysql_query("select * from Washer order by rand() limit 1", $link);

    while($row = mysql_fetch_array($result)) {
    echo '<'.$row['thequote'].'>';
    }
    mysql_free_result($result);
?>