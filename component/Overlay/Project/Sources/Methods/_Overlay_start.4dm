//%attributes = {"invisible":true}
#DECLARE($state : Object; $prepared : Object; $callback : Object; $stdin : Boolean; $png : Blob; $options : Object)
var OVERLAY_Operation : cs._OverlayOperation
OVERLAY_Operation:=cs._OverlayOperation.new($state; $callback; $options)
Use ($state)
	$state.worker:=Current process
End use
OVERLAY_Operation.start($prepared; $stdin; $png)
