//%attributes = {"invisible":true}
#DECLARE($callback : Object; $result : Object)
If (OB Instance of($callback; 4D.Function))
	$callback.call(Null; $result)
Else
	$callback.formula.call($callback.context; $result)
End if
