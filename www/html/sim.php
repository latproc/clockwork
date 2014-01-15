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
						var x_pos = res[i].x_pos;
						var y_pos = res[i].y_pos;
						var z_pos = res[i].z_pos;
						obj.position.x = x_pos;
						if (res[i].active != "true") {
							obj.position.y = -10000; //y_pos;
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
		<script>
			function makeCube(x,y,z,c) {
	            var geometry = new THREE.CubeGeometry(x,y,z);
	            var material = new THREE.MeshBasicMaterial( { color: c } );
	            return new THREE.Mesh( geometry, material );
			}
			function makeObj(model) {
				if (model.model == "beam") {
					return makeCube(10, 10, 1000, 0xff0000);
				}
				else if (model.model == "presence") {
					return makeCube(1000, 10, 10, 0xff0000);
				}
				else if (model.model == "bale") {
					return makeCube(1200, 500, 500, 0x0000ff);
				}
				else if (model.model == "conveyor") {
					var len = 500;
					if (typeof model.x_len != "undefined") len = model.x_len;
					return makeCube(len, 50, 700, 0x8080cc);
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

			camera.position.y = 800;
			camera.position.x = 1000;
            camera.position.z = 5000;
            
            function render() {
            	requestAnimationFrame(render);
                //cube.rotation.x += 0.01;
                //cube.rotation.y += 0.01;
            	renderer.render(scene, camera);
            }
            render();		
			</script>
			<div class="info"></div>
	</body>
</html>
EOD;
