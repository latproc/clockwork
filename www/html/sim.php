<?php

$title = "Clockwork Model Display";
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
		<script>
		function refresh(){
				$.get("monitor.php", { list: "json", tab: "Vis3D"}, 
			function(data){ 
				res=JSON.parse(data);
				//$("#xx").html("<p>AJAX result</p><pre>"+ data+ "</pre>");
				for (var i = 0; i < res.length; i++) {
					if (typeof res[i].model !== "undefined" 
						&& typeof res[i].id != "undefined") {
						//alert(i);
						var obj;
						var name = res[i].id;
						if (typeof elements[name] == "undefined") {
							obj = makeObj(res[i]);
							elements[name] = obj;
							if (typeof obj == "undefined") alert("make failed");
							else scene.add(obj);
						}
						else {
							obj = elements[name];
							if (typeof obj == "undefined") alert("get failed");
							else scene.add(obj);
						}
						if (res[i].model == "beam" || res[i].model == "presence") {
							if (res[i].STATE == "on")
								obj.material.color.setHex(0x00ff00);
							else
								obj.material.color.setHex(0xff0000);
						}
						if (typeof res[i].color != "undefined") {
							if (res[i].color == "red")
								obj.material.color.setHex(0xff0000);
							else if (res[i].color == "green")
								obj.material.color.setHex(0x00ff00);
						}
						var x_pos = res[i].x_pos;
						var y_pos = res[i].y_pos;
						var z_pos = res[i].z_pos;
						obj.position.x = x_pos;
						if (res[i].active != "true") {
							obj.position.y = -30000; //y_pos;
						}
						else
							obj.position.y = y_pos;
						obj.position.z = z_pos;
						if (typeof res[i].x_offset != "undefined")
							obj.position.x += res[i].x_offset;
						if (typeof res[i].y_offset != "undefined")
							obj.position.y += res[i].y_offset;
						if (typeof res[i].z_offset != "undefined")
							obj.position.z += res[i].z_offset;
					}
				}
				$(".info").each(function(){ 
					//$(this).html("<div>Loader pos: " + cube.position.y + "</div>") 
				});
				//render();
			});
			setTimeout("refresh()", 200);
		}
		$(function() {
			$(".info").each(function(){ 
				//$(this).html("<div>Loader pos: " + cube.position.y + "</div>") 
			});
			refresh();
		})
		</script>
	</head>
	<body>
		<script src="js/three.js"></script>
		<script src="js/OrbitControls.js"></script>
		<script>
			function makeCube(x,y,z,c) {
	            var geometry = new THREE.CubeGeometry(x,y,z);
	            var material = new THREE.MeshBasicMaterial( { color: c, opacity: 0.8, transparent: true } );
	            return new THREE.Mesh( geometry, material );
			}
			function makeObj(model) {
				if (model.model == "beam") {
					return makeCube(10, 10, 1000, 0xff0000);
				}
				else if (model.model == "presence") {
					var len = 1000;
					if (typeof model.x_len != "undefined") len = model.x_len; 
					return makeCube(len, 10, 10, 0xff0000);
				}
				else if (model.model == "bale") {
					return makeCube(1200, 500, 500, 0x0000ff);
				}
				else if (model.model == "conveyor") {
					var len = 500;
					if (typeof model.x_len != "undefined") len = model.x_len;
					return makeCube(len, 50, 700, 0x8080cc);
				}
				else if (model.model == "carriage") {
					var xlen = 1200;
					var zlen = 300;
					if (typeof model.x_len != "undefined") xlen = model.x_len;
					if (typeof model.z_len != "undefined") zlen = model.z_len;
					return makeCube(xlen, 50, zlen, 0x0080cc);
				}
				else if (model.model == "cylinder") {
					var ylen = 100;
					if (typeof model.y_len != "undefined") ylen = model.y_len;
					return makeCube(50, ylen, 50, 0xcc00cc);
				}
				else {
					alert("unknown model: " + model);
					return makeCube(50, 50, 50, 0xcccccc);
				}
			}
		
			scene = new THREE.Scene();
      var camera = new THREE.PerspectiveCamera( 75, window.innerWidth / window.innerHeight, 0.1, 20000 );
			elements = [];

      var renderer = new THREE.WebGLRenderer();
      renderer.setSize( window.innerWidth, window.innerHeight );
      document.body.appendChild( renderer.domElement );

			camera.position.y = 1500;
			camera.position.x = 7000;
      camera.position.z = -2000;
			camera.lookAt(new THREE.Vector3( -3000, 0, 0 ));
      controls = new THREE.OrbitControls(camera, renderer.domElement);      
      function render() {
       	requestAnimationFrame(render);
       	renderer.render(scene, camera);
				controls.update();
      }
      render();		
			</script>
			<div class="info"></div>
	</body>
</html>
EOD;
