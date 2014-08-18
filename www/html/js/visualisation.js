function debugMessage(msg) { $("#debug").append('<pre>' + msg + '</pre>')}
function getFabricObjectbyID(canvas, id) {
	var objects = canvas.getObjects();
	for (i=0; i<objects; i<objects.length) {
		if (objects[i].get('id') == id) return objects[i];
	}
	return null
}
function draw() {
	var c = $("#canvas")
	var ctx = c[0].getContext("2d");
	ctx.font = "30px Arial";
	ctx.fillText("Hello World",10,50);
	JSON.stringify(ctx);
}
function objMouseDown(options) {
	if (options.target)
		last_click = options.target
}
function objMouseUp(options) {
	if (!canvas.selection && options.target && options.target == last_click) 
		$.post('vis.php', {command:"click", name:options.target.item(1).text},
			function(data){ debugMessage(data) }
		)
}
function initfabric() {
	canvas = new fabric.Canvas('canvas');
	canvas.selection = false;
	canvas.setHeight(window.innerHeight * 0.9);
	canvas.setWidth(window.innerWidth);
	canvas.on('mouse:down', function(options) {
		if (options.target) {
			objMouseDown(options)
			options.e.stopPropagation()
		}
	})
	canvas.on('mouse:up', function(options) {
		if (options.target) {
			objMouseUp(options)
			options.e.stopPropagation()
		}
	})
	canvas.renderAll()
}
function tryfabric() {
	// create a rectangle object
	var rect = new fabric.Rect({
	  left: 100,
	  top: 100,
	  fill: 'red',
	  width: 20,
	  height: 20
	});
	rect.set('id','rect');

	// "add" rectangle onto canvas
	canvas.add(rect);
	debugMessage(JSON.stringify(rect));
	debugMessage(JSON.stringify(getFabricObjectbyID(canvas, 'rect')))
	rect.set('fill', 'blue')
}
function toggleEdit() {
	canvas.selection = !canvas.selection;
	canvas.forEachObject(function(o) {
	  o.selectable = canvas.selection;
	});
}
function editOff() {
	canvas.selection = false
	canvas.forEachObject(function(o) {
	  o.selectable = false
	});
}
	
	function refresh() {
		$.post("vis.php", { list: "json", tab: "Vis2D"}, 
			function(data){  
				if (data.length == 0) alert('failed to communicate with clockwork');
				else {
					try {
						res=JSON.parse(data); 
						if (typeof canvas == 'undefined') initfabric()
						if (typeof items == 'undefined') items = []
						for (var i = 0; i < res.length; i++) {
							var label = res[i].name
							if ('label' in res[i]) label = res[i].label
							// save or update the item
							if (label in items) {
								items[label].info = items[i]
							}
							else {
								var wid = (label.length)*20;
								var obj = null
								var shape = 'rectangle'
								if ('shape' in res[i]) shape = res[i].shape;
								if (shape == "rectangle")
									obj = new fabric.Rect({
										originX: 'center', originY: 'center',
										fill: 'red',
										width: wid,
										height: 40
									});
								else if (shape == 'circle')
									obj = new fabric.Circle({
									  radius: wid/2, fill: 'blue', originX: 'center', originY: 'center'
									});
								
								var text = new fabric.Text(label, { originX: 'center', originY: 'center', fontSize: 24 });
								var grp = new fabric.Group([obj, text],{ left: 100, top: 100 })
								grp.selectable = canvas.selection
								canvas.add(grp);
								items[label] = {info:res[i],gr:grp}
							}
							if ('colour' in res[i]) {
								items[label].gr.item(0).set('fill', res[i].colour)
							}
						}
						canvas.renderAll()
					}
					catch (e) {
						if (typeof last_err_time == "undefined") {last_err_time = Date.now(); } 
						$("#errors").append('<pre>' 
							+ (Date.now()-last_err_time) 
							+ " " + e.message + '\n' + data + '</pre>');
						last_err_time = Date.now();
					}
				}
		}
		)
		setTimeout("refresh()", 200);
	}
	function load() {
		$.post("vis.php", { load_layout: true }, 
			function(data){ 
				res = JSON.parse(data); 
				canvas.clear().renderAll()
				canvas.loadFromJSON(res)
				editOff();
				if (typeof items == 'undefined') items = []
				var objs = canvas.getObjects()
				for (i=0; i<objs.length; i++) {
					if (objs[i].type == 'group') {
						var grp = objs[i]
						if (grp.item(1).type == 'text') {
							var lbl = grp.item(1).text
							if (lbl in items)
								items[lbl].gr = grp
							else
								items[lbl] = {item:null, gr:grp}
						}
					}
				}
				canvas.renderAll()
			}
		)				
	}
$(function() {
	var LabeledRect = fabric.util.createClass(fabric.Rect, {

	  type: 'labeledRect',

	  initialize: function(options) {
	    options || (options = { });

	    this.callSuper('initialize', options);
	    this.set('label', options.label || '');
	  },

	  toObject: function() {
	    return fabric.util.object.extend(this.callSuper('toObject'), {
	      label: this.get('label')
	    });
	  },

	  _render: function(ctx) {
	    this.callSuper('_render', ctx);

	    ctx.font = '20px Helvetica';
	    ctx.fillStyle = '#333';
	    ctx.fillText(this.label, -this.width/2, -this.height/2 + 20);
	  }
	});
	
	$("#update_btn").click(function(e) {
		refresh()
		return false
	})
	$("#save_btn").click(function(e) {
		if (typeof canvas == "undefined") return false;
		$.post("vis.php", { save_layout: true, layout: JSON.stringify(canvas)}, 
			function(data){ debugMessage(data) }
		)
		return false;
	})
	$("#load_btn").click(function(e) {
		if (typeof canvas == "undefined") return false;
		load()
		return false;
	})
	$("#editmode_btn").click(function(e) {
		e.preventDefault();
		toggleEdit()
		if (canvas.selection) $(this).addClass('selected'); else $(this).removeClass('selected');
		return false
	})
	initfabric();
	load();
	refresh();			
})
