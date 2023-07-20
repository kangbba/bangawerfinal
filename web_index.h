const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Example</title>
</head>
<body>
<table>
<tr>
<td>
<button onClick="record('/record1')" id="record1">RECORD 1</button>
<button onClick="play('/sound1.wav')" id="play1">PLAY 1</button>
<div id="status1">OK<div>
</td>
</tr>
<tr>
<td>
<button onClick="record('/record2')" id="record2">RECORD 2</button>
<button onClick="play('/sound2.wav')" id="play2">PLAY 2</button>
<div id="status2">OK<div>
</td>
</tr>
<tr>
<td>
<button onClick="record('/record3')" id="record3">RECORD 3</button>
<button onClick="play('/sound3.wav')" id="play3">PLAY 3</button>
<div id="status3">OK<div>
</td>
</tr>
</table>
<iframe id="ifr" style="display:none"></iframe>
</body>
</html>
<script type="text/javascript">
if (window.HTMLAudioElement)
{
  var player = new Audio('');
  var counter;
  function play(url)
  {
    if (player.paused || url != player.src)
    {
      if (player.canPlayType('audio/mp3'))
      {
        player.src = url;
      }
      player.play();
    }
    else
    {
      player.pause();
    }
  }
  
  function record(url)
  {
    ifr.src = location.origin + url;
    if ( url == '/record1' )
    {
      status1.innerHTML = "RECORDING";
    }
    if ( url == '/record2' )
    {
      status2.innerHTML = "RECORDING";
    }
    if ( url == '/record3' )
    {
      status3.innerHTML = "RECORDING";
    }
    counter = setInterval(timer,10000);
  }

  function timer()
  {
    clearInterval(counter);
    status1.innerHTML = "OK";
    status2.innerHTML = "OK";
    status3.innerHTML = "OK";
  }
}
</script >
)rawliteral";
