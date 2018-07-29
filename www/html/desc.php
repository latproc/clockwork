<?php

print <<<EOD
<html>
<head>
<script src="js/jquery-1.7.min.js"></script>
<script>
$(function() {
	$("#hideinfo").click( function(){
		$(this).css("display","none"); 
		$("#info").css("display","none")
	})
$("#x").click(function(){
	$.get("index.php", { describe: $(this).html },
        function(data){
			$("#info").html("<pre>"+data+"</pre>");
			$("#info").css("display","block");
			$("#hideinfo").css("display","block");
		})
	 })
	
})
</script>
</head>
<body>
<div style="position:relative">
  <div id="hideinfo" style="position:absolute;left:600px;top:80px;display:none;z-index:100;">hide</div>
  <div id="info" style="position:absolute;left:100px;top:100px;width:600px;height:400px;overflow:scroll;display:none;z-index:100;" >
  </div>
</div>
<div id="x">m</div>
</body>
</html>
EOD;
