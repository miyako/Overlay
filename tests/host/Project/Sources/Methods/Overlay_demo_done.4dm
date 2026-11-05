#DECLARE($result : Object)
var $diagnostic : Object
If ($result.ok)
	ALERT("Overlay: "+$result.value.outcome+" ("+$result.value.reason+")")
Else
	$diagnostic:=Overlay_error_details($result)
	If (Not($diagnostic.cancelled))
		ALERT($diagnostic.message)
	End if
End if
