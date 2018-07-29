<html>
	<head>
		<title>%title%</title>
		<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" >
		<script type="text/javascript" src="js/jquery-1.9.1.js"></script>
		<script type="text/javascript" src="js/jquery-ui-1.10.3.custom/js/jquery-ui-1.10.3.custom.js"></script>
		<script type="text/javascript" src="js/fabric.js"></script>
		<link type="text/css" href="js/jquery-ui-1.10.3.custom/css/smoothness/jquery-ui-1.10.3.custom.min.css" rel="stylesheet" />
		<style>
		body { font-family: Helvetica, Arial, sans-serif; background-color:#e8e8e8 }
		.visualisation { height:80%; width:100%; background-color:#d0d0d0}
		#canvas {   }
		table { width:100% }
		.error_message { font-size:24px; color:#f44; }
		.center { text-align:center; }
		.info {z-index: 100; color:white; position: absolute; top: 10px; width: 100%; display:block;}
		.selected { background-color:orange}
		#update_btn { display:none }
		#editmode_btn { border:1px solid black; border-radius:5px; padding:2px; margin-left:3px; cursor:default}
		</style>
		<script type="text/javascript" src="js/visualisation.js"></script>
	</head>
<body>
<div class="toolbar">
<form action=? method="POST">
<button id="update_btn">Regenerate</button>
<button id="save_btn">Save</button>
<button id="load_btn">Load</button>
<button id="delete_btn">Delete</button>
<span id="editmode_btn">Edit</button>
</form>
</div>
<div class="visualisation">
<canvas id="canvas" ></canvas>
</div>
<div id="errors"><h2>Errors</h2></div>
<div id="debug"><h2>Debug</h2></div>
</body>
</html>
