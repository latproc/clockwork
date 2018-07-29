<?php

$title = "Machine Settings";
print <<<EOD
<html>
	<head>
		<title>$title</title>
		<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" >
		<script type="text/javascript" src="js/jquery-1.10.2.min.js"></script>
		<script type="text/javascript" src="js/jquery-ui-1.10.3.custom/js/jquery-ui-1.10.3.custom.min.js"></script>
		<link type="text/css" href="js/jquery-ui-1.10.3.custom/css/smoothness/jquery-ui-1.10.3.custom.min.css" rel="stylesheet" />
		<style>
		body { font-family: Helvetica, Arial, sans-serif }
		canvas { width: 100%; height: 100% }
		table { width:100% }
		.error_message { font-size:24px; color:#f44; }
		.center { text-align:center; }
		.info {z-index: 100; color:white; position: absolute; top: 10px; width: 100%; display:block;}
		</style>
		<script type="text/javascript">
		$().function() {
			$("#update_btn").click(function(e) {
				alert("TEST");
				e.stopPropagation();
			})
		})
		</script>
	</head>
<body>
<form action='monitor.php' method=POST>
<input type=hidden name='update' value='true' />
<p><label name='machine' value='test'>Machine</label><span>: test</span></p>
<p><label name='property' value='test'>Property</label><span>: prop1</span></p>
<p><label>Value:</label> <input name='val' value='2' />
<button id="update_btn" value='Update'>Test</button>
</form>
</body>
</html>
EOD;

