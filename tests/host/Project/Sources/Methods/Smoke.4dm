//%attributes = {"invisible":true}
var $ignored; $ticks : Integer
var $parameter; $target : Text
var $request; $result; $report; $callback; $callbackState; $context : Object
var $tool : cs.Overlay.Overlay
var $operation : cs.Overlay._OverlayOperation
var $requestFile; $statusFile; $renamedExecutable : 4D.File
var $handle; $modal; $cancelled; $binary; $nullShow : cs.Overlay.OverlayHandle
var $png : Blob
var HOST_StatusFile : 4D.File
var HOST_Phase : Text
var HOST_PreviousHandler : Text
$ignored:=Get database parameter(User param value; $parameter)
If (Is Windows)
	$requestFile:=File($parameter; fk platform path)
Else
	$requestFile:=File($parameter; fk posix path)
End if
$request:=JSON Parse($requestFile.getText())
If (Is Windows)
	$statusFile:=File($request.status; fk platform path)
Else
	$statusFile:=File($request.status; fk posix path)
End if
$statusFile.setText("{\"success\":false,\"finished\":false,\"phase\":\"starting\"}")
HOST_StatusFile:=$statusFile
HOST_Phase:="constructor"
HOST_PreviousHandler:=Method called on error(ek local)
ON ERR CALL("Host_error"; ek local)
$target:="macOS-universal"
If (Is Windows)
	$target:="Windows-x64"
End if
$tool:=cs.Overlay.Overlay.new($target)
$report:=New object("success"; True; "finished"; False; "cases"; New collection)
$report.hostCompiled:=Is compiled mode
$report.compiled:=$tool._cli.compiled
If ($request.expectCompiled#$report.compiled)
	$report.success:=False
End if
HOST_Phase:="native metadata"
$result:=$tool.version()
$report.version:=$result
If (Not($result.ok))
	$report.success:=False
Else
	If ($result.value.version#"0.1.0")
		$report.success:=False
	End if
End if
$result:=$tool.capabilities()
$report.capabilities:=$result
$report.cases.push($result)
If (Not($result.ok))
	$report.success:=False
End if
$report.screens:=$tool.screens()
If (Not($report.screens.ok))
	$report.success:=False
End if
HOST_Phase:="renamed executable"
If (Is Windows)
	$renamedExecutable:=File($request.executable; fk platform path)
Else
	$renamedExecutable:=File($request.executable; fk posix path)
End if
$report.renamedExecutable:=cs.Overlay._Process.new().execute($renamedExecutable; New collection("--version"))
If (Not($report.renamedExecutable.ok))
	$report.success:=False
End if
HOST_Phase:="async headless validation"
$operation:=cs.Overlay._OverlayOperation.new(Null; Null; New object("timeoutMs"; 0))
$report.noTimeout:=Not(OB Is defined($operation; "timeout"))
$operation:=cs.Overlay._OverlayOperation.new(Null; Null; New object)
$report.noDefaultSafetyCap:=Not(OB Is defined($operation; "timeout"))
$operation:=cs.Overlay._OverlayOperation.new(Null; Null; New object("timeoutMs"; 3600000; "fadeInMs"; 60000; "fadeOutMs"; 60000))
$report.positiveTimeout:=$operation.timeout
$operation:=Null
$callbackState:=New shared object("count"; 0; "results"; New shared collection)
$callback:=New object("formula"; Formula(Overlay_smoke_callback(This; $1)); "context"; $callbackState)
$ticks:=Tickcount
$handle:=$tool.show(New object("image"; $requestFile; "mode"; "invalid-mode"); $callback)
$modal:=$tool.show(New object("image"; $requestFile; "modal"; True); $callback)
TEXT TO BLOB("not a PNG"; $png; UTF8 text without length)
$png{0}:=137
$png{1}:=0
$png{2}:=255
$binary:=$tool.showBlob($png; New object; $callback)
$cancelled:=$tool.show(New object("image"; $requestFile; "mode"; "invalid-mode"; "timeoutMs"; 0); $callback)
$nullShow:=$tool.show(Null; $callback)
$report.cancelAccepted:=$cancelled.cancel()
$report.cancelRepeated:=$cancelled.cancel()
$report.enqueueTicks:=Tickcount-$ticks
$report.caller:=Current process
$statusFile.setText(JSON Stringify($report))
$context:=New object("report"; $report; "target"; $target; "parameter"; $parameter; "requestFile"; $requestFile; "statusFile"; $statusFile; "handle"; $handle; "modal"; $modal; "binary"; $binary; "cancelled"; $cancelled; "nullShow"; $nullShow; "callbackState"; $callbackState; "deadline"; Tickcount+1800)
Repeat
	DELAY PROCESS(Current process; 1)
Until (($handle.done & $modal.done & $binary.done & $cancelled.done & $nullShow.done & ($callbackState.count=5)) | (Tickcount>$context.deadline))
Overlay_smoke_continue($context)
ON ERR CALL(HOST_PreviousHandler; ek local)
