//%attributes = {"invisible":true}
#DECLARE($context : Object)
var $ignored : Boolean
var $name; $target; $parameter; $folderPath; $previous; $json; $terminal : Text
var $report; $result : Object
var $requestFile; $statusFile; $pngFile : 4D.File
var $png : Blob
var $tool : cs.Overlay.Overlay
var $handle; $modal; $binary; $cancelled : cs.Overlay.OverlayHandle
var $invalid : Variant
var HOST_StatusFile : 4D.File
var HOST_Phase : Text
var HOST_PreviousHandler : Text
$report:=$context.report
$target:=$context.target
$parameter:=$context.parameter
$requestFile:=$context.requestFile
$statusFile:=$context.statusFile
$handle:=$context.handle
$modal:=$context.modal
$binary:=$context.binary
$cancelled:=$context.cancelled
$tool:=cs.Overlay.Overlay.new($target)
$previous:=Method called on error(ek local)
ON ERR CALL("Host_error"; ek local)
$report.invalid:=$handle.result
$report.modal:=$modal.result
$report.binary:=$binary.result
$report.cancelled:=$cancelled.result
$report.nullShow:=$context.nullShow.result
$report.callbackCount:=$context.callbackState.count
$report.callbackResults:=New collection
For each ($json; $context.callbackState.results)
	$report.callbackResults.push(JSON Parse($json))
End for each
$report.asyncDone:=$handle.done & $modal.done & $binary.done & $cancelled.done & $context.nullShow.done
$report.pruned:=($handle._state.worker=0) & ($modal._state.worker=0) & ($binary._state.worker=0) & ($cancelled._state.worker=0) & ($context.nullShow._state.worker=0)
$report.cancelAfterDone:=$handle.cancel()
If (Not($report.asyncDone) | ($report.callbackCount#5))
	$report.success:=False
	$ignored:=$handle.cancel()
	$ignored:=$modal.cancel()
	$ignored:=$binary.cancel()
	$ignored:=$cancelled.cancel()
End if
HOST_Phase:="missing executable"
$report.missing:=cs.Overlay._CLI.new("missing-cli"; $target).execute(New collection("--version"))
$report.unsupported:=cs.Overlay.Overlay.new("unsupported-arm-target").version()
HOST_Phase:="launch failure"
$report.launch:=cs.Overlay._Process.new().execute($requestFile; New collection)
HOST_Phase:="malformed protocol"
$report.protocol:=$tool._decode(New object("ok"; True; "stdout"; "not json"; "stderr"; ""; "exitCode"; 0); "show")
$report.protocolTypes:=New collection
For each ($name; New collection("{\"protocol\":true,\"message\":\"invalid\"}"; "{\"protocol\":false,\"message\":\"invalid\"}"))
	$report.protocolTypes.push($tool._decode(New object("ok"; True; "stdout"; $name; "stderr"; ""; "exitCode"; 0); "show"))
End for each
$report.protocolNull:=$tool._decode(New object("ok"; True; "stdout"; "{\"protocol\":null,\"message\":\"invalid\"}"; "stderr"; ""; "exitCode"; 0); "show")
$report.protocolScalars:=New collection
For each ($name; New collection("true"; "false"; "null"; "1"; "[]"))
	$report.protocolScalars.push($tool._decode(New object("ok"; True; "stdout"; $name; "stderr"; ""; "exitCode"; 0); "show"))
End for each
$report.terminalTypes:=New collection
For each ($name; New collection("{\"protocol\":1,\"outcome\":true,\"reason\":\"click\",\"x\":0,\"y\":0}"; "{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":false,\"x\":0,\"y\":0}"; "{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"click\",\"x\":null,\"y\":0}"; "{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"click\",\"x\":0,\"y\":\"0\"}"; "{\"protocol\":1,\"outcome\":\"unknown\",\"reason\":\"timeout\"}"; "{\"protocol\":1,\"outcome\":\"dismissed\",\"reason\":\"\"}"; "{\"protocol\":1,\"outcome\":\"dismissed\",\"reason\":\"timeout\",\"x\":0,\"y\":0}"; "{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"timeout\",\"x\":0,\"y\":0}"))
	$report.terminalTypes.push($tool._decode(New object("ok"; True; "stdout"; $name+Char(10); "stderr"; ""; "exitCode"; 0); "show"))
End for each
$report.terminalCoordinates:=New collection
For each ($name; New collection("{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"click\",\"x\":-1,\"y\":0}"; "{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"click\",\"x\":0,\"y\":-1}"; "{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"click\",\"x\":8193,\"y\":0}"; "{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"click\",\"x\":0,\"y\":8193}"; "{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"click\",\"x\":1e309,\"y\":0}"; "{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"click\",\"x\":NaN,\"y\":0}"))
	$report.terminalCoordinates.push($tool._decode(New object("ok"; True; "stdout"; $name+Char(10); "stderr"; ""; "exitCode"; 0); "show"))
End for each
$terminal:="{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"click\",\"x\":1.25,\"y\":2}"
$report.terminalFraming:=New collection
For each ($name; New collection($terminal; $terminal+Char(10)+$terminal+Char(10); $terminal+$terminal+Char(10); $terminal+Char(10)+" "; "{\"protocol\":1,\n\"outcome\":\"dismissed\",\"reason\":\"timeout\"}\n"; $terminal+Char(13)+Char(10); $terminal+Char(10)+Char(10)))
	$report.terminalFraming.push($tool._decode(New object("ok"; True; "stdout"; $name; "stderr"; ""; "exitCode"; 0); "show"))
End for each
// Decoder-unit fixtures only; these are not evidence of native GUI events.
$report.terminalDecoder:=New collection
For each ($name; New collection($terminal+Char(10); "{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"click\",\"x\":8192,\"y\":2e0}\n"; "{\"protocol\":1,\"outcome\":\"dismissed\",\"reason\":\"timeout\"}\n"))
	$report.terminalDecoder.push($tool._decode(New object("ok"; True; "stdout"; $name; "stderr"; ""; "exitCode"; 0); "show"))
End for each
HOST_Phase:="File and Folder arguments"
BASE64 DECODE("iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAEUlEQVR4nGP4z8DwH4QZYAwAR8oH+WdZbrcAAAAASUVORK5CYII="; $png)
$pngFile:=$requestFile.parent.file("valid PNG 日本語.png")
$pngFile.setContent($png)
$report.validate:=cs.Overlay._CLI.new("overlay-cli"; $target).execute(New collection("validate"; "--image"; $pngFile; "--x"; "0"; "--y"; "0"))
If (Not($report.validate.ok))
	$report.success:=False
End if
$report.fileArgument:=cs.Overlay._CLI.new("overlay-cli"; $target).execute(New collection("arguments"; "--value"; $requestFile))
$folderPath:=$requestFile.parent.path
If (Is Windows)
	$folderPath:=$requestFile.parent.platformPath
End if
$report.folderArgument:=cs.Overlay._CLI.new("overlay-cli"; $target).execute(New collection("arguments"; "--value"; $requestFile.parent))
If ($report.fileArgument.ok)
	$report.fileRoundtrip:=JSON Parse($report.fileArgument.stdout).value=$parameter
Else
	$report.fileRoundtrip:=False
End if
If ($report.folderArgument.ok)
	$report.folderRoundtrip:=JSON Parse($report.folderArgument.stdout).value=$folderPath
Else
	$report.folderRoundtrip:=False
End if
HOST_Phase:="Text arguments"
$report.textRoundtrips:=New collection
For each ($name; New collection("path with spaces"; "quote \" and \\ slash"; "line\nbreak"; "日本語"; "$() ; & % !"))
	$result:=cs.Overlay._CLI.new("overlay-cli"; $target).execute(New collection("arguments"; "--value"; $name))
	$report.textRoundtrips.push(New object("result"; $result; "equal"; False))
	If ($result.ok)
		$report.textRoundtrips[$report.textRoundtrips.length-1].equal:=JSON Parse($result.stdout).value=$name
	End if
End for each
HOST_Phase:="scalar arguments"
$report.rejectedArguments:=New collection
For each ($invalid; New collection(123; 1.25; True; False; Null; Current date; Current time; New collection; New object))
	$result:=cs.Overlay._Process.new().execute($requestFile; New collection($invalid))
	$report.rejectedArguments.push(New object("type"; Value type($invalid); "result"; $result))
End for each
HOST_Phase:="NUL arguments"
$report.rejectedNul:=New collection
For each ($name; New collection(Char(0)+"prefix"; "middle"+Char(0)+"suffix"; "trailing"+Char(0)))
	$report.rejectedNul.push(cs.Overlay._Process.new().execute($requestFile; New collection($name)))
End for each
$report.missingFile:=cs.Overlay._Process.new().execute(Null; New collection)
$report.missingArguments:=cs.Overlay._Process.new().execute($requestFile; Null)
$report.nullOptions:=$tool._arguments(Null; False)
If (Not($report.fileRoundtrip & $report.folderRoundtrip))
	$report.success:=False
End if
For each ($result; $report.textRoundtrips)
	If (Not($result.equal))
		$report.success:=False
	End if
End for each
$report.finished:=True
ON ERR CALL($previous; ek local)
$statusFile.setText(JSON Stringify($report))
HOST_StatusFile:=Null
HOST_Phase:=""
HOST_PreviousHandler:=""
QUIT 4D
