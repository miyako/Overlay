//%attributes = {"invisible":true}
#DECLARE($state : Object; $callback : Object)
var $operation : cs.Overlay._OverlayOperation
var $png : Blob
Use ($state)
	$state.worker:=Current process
End use
$operation:=cs.Overlay._OverlayOperation.new($state; $callback; New object)
$operation.start(New object("ok"; False; "error"; New object("code"; "test_launch"; "message"; "Cancelled invocation must not reach launch")); False; $png)
