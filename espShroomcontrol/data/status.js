var tt =0;
var th = 0;
var init = false;
var TimeMap = new Map();
var TimeNameToId = new Map();
var DateMap = new Map();
var DateNameToId=new Map();
var lastSelectStart;
var lastSelectEnd;
var newid= 0;

class TimeConfig
{
  constructor(id)
  {
    this.id = id;
  }

  setName(name)
  {
    this.name=name;
  }
  setStart(starttime)
  {
    this.start = starttime;
  }
  setEnd(endtime)
  {
    this.end = endtime;
  }

  fillBoxes()
  {
    $("[name='timestart']").val(this.start);
    $("[name='timeend']").val(this.end);
    $("[name='timespanname']").val(this.name);
  }

  toJSON()
  {
    var tobj = new Object;
    tobj["id"] = this.id;
    tobj["name"] = this.name;
    tobj["start_h"] = parseInt(this.start.split(":")[0]);
    tobj["start_m"] = parseInt(this.start.split(":")[1]);
    tobj["end_h"] = parseInt(this.end.split(":")[0]);
    tobj["end_m"] = parseInt(this.end.split(":")[1]);
    // var json = `{\"id\":${this.id}, \"name\":\"${this.name}\",\"starth\":${start_h},\"startm\":${start_m},\"endh\":${end_h},\"endm\":${end_m}}`;
    return tobj;
  }
}

class DateConfig
{
  constructor(id)
  {
    this.id = id;
  }

  setName(name)
  {
    this.name=name;
  }
  setStart(starttime)
  {
    this.start = starttime;
  }
  setEnd(endtime)
  {
    this.end = endtime;
  }

  setTimeId(id)
  {
    this.timeid = id;
  }

  fillBoxes()
  {
    $("[name='daterange']").val(`${momentFormatted(this.start)} - ${momentFormatted(this.end)}`); // TODO Format differently
    $("[name='datename']").val(this.name);
    $("[name='ConfigTimes']").val(TimeMap.get(this.timeid).name);
  }

  toJSON()
  {
    var tobj = new Object;
    tobj["id"] = this.id;
    tobj["name"] = this.name;
    tobj["start"] = this.start.dayOfYear();
    tobj["end"] = this.end.dayOfYear();
    tobj["timeid"] = this.timeid;
    // var json = `{\"id\":${this.id}, \"name\":\"${this.name}\",\"start\":${this.start.dayOfYear()},\"end\":${this.end.dayOfYear()},\"timeid\":${this.timeid}}`;
    return tobj;
  }
}

function momentFormatted(date)
{
  return date.format('DD/MM/YYYY');
}

var selectedTimeConfig="";
var selectedDateConfig="";
function updateStatus() {
    $.ajax({
      method: "GET",
      url: window.location.origin+"/get/status",
      success: function(data) {
        $('.temp-value').empty().append(data.tval);
        $('.humid-value').empty().append(data.hval);
        $('.light-value').empty().append(data.light);
        $('.fan-value').empty().append(data.fan);

        $('.status-box').append(data.status);
        $('.status-box').scrollTop(99999);
      },
      complete: function(obj, status){
        setTimeout(updateStatus, 1000); 
      }
    });
    // you could choose not to continue on failure...
}
  
function submitTarget()
{
  var tobj = new Object();
  $.ajax({
    method: "PUT",
    url: window.location.origin+"/update/target",
    data: tobj,
    fail: function(data){
      $('.temp-target').val(data.ttarget);
      $('.humid-target').val(data.htarget);
    }
  });
}

function handleEnter(e)
{
  var keycode = (e.keyCode ? e.keyCode : e.which);
    if (keycode == '13') {
        var tobj = new Object();
        tobj.input = $('.serial-input').val();
        $.ajax({
          method: "PUT",
          url: window.location.origin+"/update/serialinput",
          data: tobj,
          success: function(data){
            $('.serial-input').val("");
          }
        });
    }
}

function parseTimeJSON(obj)
{
  obj.forEach(function(range)
  {
    newTimeRange(range.id, range.name, `${range.starth}:${range.startm}`, `${range.endh}:${range.endm}`);
    lastname = range.name;
    if(newid <= range.id)
      newid = range.id+1;
  });
  onTimesSelect("");
}

function parseDateJSON(obj)
{
  obj.forEach(function(range)
  {
    var start = moment().dayOfYear(range.start);
    var end = moment().dayOfYear(range.end);
    newDateRange(range.id, range.name, start,end, range.timeid);
    lastname=range.name;
    if(newid <= range.id)
      newid = range.id+1;
  });

  onDatesSelect("");
}


$(document).ready(function() {

    $('input[name="timestart"]').timepicker({
       showLeadingZero: false,
       onSelect: tpStartSelect
   });
   $('#timepicker_end').timepicker({
       showLeadingZero: false,
       onSelect: tpEndSelect
   });
   var today = new Date();
    $('input[name="daterange"]').daterangepicker({
      opens: 'left',
      autoApply: true,
      locale:{format:"DD/MM/YYYY"},
      startDate: today.getDate() + '/' + (today.getMonth()+1)+'/'+ today.getFullYear()
    }, function(start, end, label) {
      console.log("A new date selection was made: " + start.format('YYYY-MM-DD') + ' to ' + end.format('YYYY-MM-DD'));
      lastSelectEnd=end;//.format('DD/MM/YYYY');
      lastSelectStart=start;//.format('DD/MM/YYYY');
    });

    $('.submit-target').onclick = submitTarget;

    $.ajax({ //GET current configuration from device
      method: "GET",
      url: window.location.origin+"/get/config",
      success: function(data) {
        parseTimeJSON(data.timeranges);
        parseDateJSON(data.dateranges);
      },
      complete: function(obj, status){
        //setTimeout(updateStatus, 1000); 
      }
    });

    updateStatus();
});

// when start time change, update minimum for end timepicker
function tpStartSelect( time, endTimePickerInst ) {
  $('#timepicker_end').timepicker('option', {
      minTime: {
          hour: endTimePickerInst.hours,
          minute: endTimePickerInst.minutes
      }
  });
}

// when end time change, update maximum for start timepicker
function tpEndSelect( time, startTimePickerInst ) {
  $('#timepicker_start').timepicker('option', {
      maxTime: {
          hour: startTimePickerInst.hours,
          minute: startTimePickerInst.minutes
      }
  });
}

function getNewId()
{
  return newid++;
}

function getTimeByName(name)
{
  return TimeMap.get(TimeNameToId.get(name));
}

function getDateByName(name)
{
  return DateMap.get(DateNameToId.get(name));
}

function newTimeRange(id, name, start, end)
{
  var newmeme= new TimeConfig(id);
  TimeMap.set(id, newmeme);
  newmeme.setName(name);
  TimeNameToId.set(newmeme.name, id);
  newmeme.setStart(start);
  newmeme.setEnd(end);
  var  newopt = document.createElement("option");
  newopt.text = newmeme.name;
  var x = document.getElementById("ConfigTimeId");
  x.add(newopt);
  x.value = newmeme.name;
}

function newTimeRangeBtn()
{
  var curid = getNewId();
  var name = $('input[name="timespanname"]').val();
  var start =$('#timepicker_start').val();
  var end = $('#timepicker_end').val();
  newTimeRange(curid, name, start, end)
}

function saveTimeRange()
{
  var x = document.getElementById("ConfigTimeId");
  selectedRange = getTimeByName(x.value); 
  TimeNameToId.delete(selectedRange.name);
  selectedRange.setName($('input[name="timespanname"]').val());
  TimeNameToId.set(selectedRange.name, selectedRange.id);
  selectedRange.setStart($('#timepicker_start').val());
  selectedRange.setEnd($('#timepicker_end').val());
  x.options[x.selectedIndex].text =selectedRange.name;
}

function deleteTimeRange()
{
  var x = document.getElementById("ConfigTimeId");
  selectedRange = getTimeByName(x.value); 
  //Make sure date ranges are still OK
  for(var datething in DateMap)
  {
    if(datething[1].timeid == selectedRange.id)
    {
      //Clean up. Dunno.
      datething[1].timeid = null;
    }
  }
  TimeMap.delete(selectedRange.id);
  TimeNameToId.delete(selectedRange.name);
  x.remove(x.selectedIndex);
}

function newDateRange(id, name, start, end, timeid)
{
  var x = document.getElementById("ConfigDatesId");
  var newmeme = new DateConfig(id);
  newmeme.start = start;
  newmeme.end = end;
  newmeme.timeid = timeid;
  newmeme.name = name
  DateMap.set(newmeme.id, newmeme);
  DateNameToId.set(newmeme.name, newmeme.id);

  var option = document.createElement('option');
  option.text = newmeme.name;
  x.add(option);
  x.value = newmeme.name;

}

function newDateRangeBtn()
{
  var y = document.getElementById("ConfigTimeId");
  var timething = getTimeByName(y.value);
  //New thing
  newDateRange(getNewId(), $('input[name="datename"]').val(), lastSelectStart, lastSelectEnd,timething.id);
}

function saveDateRange()
{
  var x = document.getElementById("ConfigDatesId");
  var y = document.getElementById("ConfigTimeId");
  var timething = getTimeByName(y.value);
  if(x!=null && DateNameToId.has(x.value))
  {
    //Update existing thing
    var name = x.value;//

    var thing = getDateByName(name);
    var indexinbox = x.selectedIndex;
    thing.name = $('input[name="datename"]').val();
    x.options[indexinbox].text = thing.name;
    DateNameToId.delete(name);
    DateNameToId.set(thing.name, thing.id);
    thing.start = lastSelectStart;
    thing.end = lastSelectEnd;
    thing.timeid = timething.id;
  }
}

function deleteDateRange()
{
  var x = document.getElementById("ConfigDatesId");
  var meme = getDateByName(x.value);
  x.remove(x.selectedIndex);
  DateMap.delete(meme.id);
  DateNameToId.delete(meme.name);
}

function onDatesSelect(value)
{
  var x = document.getElementById("ConfigDatesId");
  var meme = getDateByName(x.value);
  meme.fillBoxes();
  var drp = $('input[name="daterange"]').data('daterangepicker');
  drp.setStartDate(meme.start);
  drp.setEndDate(meme.end);
  lastSelectEnd=meme.end;
  lastSelectStart = meme.start;
}

function onTimesSelect(value)
{
  var x = document.getElementById("ConfigTimeId");
  var meme = getTimeByName(x.value);
  meme.fillBoxes();
}

function sendToDevice()
{
  var tobj = {numTimes: TimeMap.size, numDates: DateMap.size, timeranges:[], dateranges:[]};
  for(let [k,v] of TimeMap)
  {
    tobj["timeranges"].push(v.toJSON());
  }
  for(let [k,v] of DateMap)
  {
    tobj["dateranges"].push(v.toJSON());
  }
  
  $.ajax({
    method: "PUT",
    url: window.location.origin+"/set/config",
    data: tobj,
    success: function()
    {
      alert("Success! :)");
    }
  });
}
