#DECLARE($result : Object)
If ($result.ok)
	ALERT("Overlay: "+$result.value.outcome+" ("+$result.value.reason+")")
Else
	ALERT("Overlay: "+$result.error.code)
End if
