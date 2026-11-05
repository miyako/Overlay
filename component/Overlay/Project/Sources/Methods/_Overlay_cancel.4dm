//%attributes = {"invisible":true}
var OVERLAY_Operation : cs._OverlayOperation
If (OVERLAY_Operation#Null)
	If (Not(OVERLAY_Operation.completed))
		If (OVERLAY_Operation.worker#Null)
			OVERLAY_Operation.worker.terminate()
		End if
	End if
End if
