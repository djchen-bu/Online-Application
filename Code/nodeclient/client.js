console.log('Client-side code running');

const getbutton = document.getElementById('getRequest');
getbutton.addEventListener('click', function(){
	$.get("http://djchen.ddns.net:3000/temp", function(response){
		document.getElementById('gettemp').innerHTML = response;
	});
});

const onbutton = document.getElementById('on');
onbutton.addEventListener('click', function(){
	$.post("http://djchen.ddns.net:3000/servo", "1")
})

const offbutton = document.getElementById('off');
offbutton.addEventListener('click', function(){
	$.post("http://djchen.ddns.net:3000/servo", "0")
})

const timebutton = document.getElementById('setTime');
timebutton.addEventListener('click', function(){
	var form = new FormData(document.getElementById("timeform"));
	var hour = form.get("hour");
	var minute = form.get("minute");
	var second = form.get("second");
	$.post("http://djchen.ddns.net:3000/time", hour+ " " + minute + " " + second);
})

const alarmbutton = document.getElementById('setAlarm');
alarmbutton.addEventListener('click', function(){
	var form = new FormData(document.getElementById("timeform"));
	var hour = form.get("hour");
	var minute = form.get("minute");
	var second = form.get("second");
	$.post("http://djchen.ddns.net:3000/alarm", hour+ " " + minute + " " + second);
})