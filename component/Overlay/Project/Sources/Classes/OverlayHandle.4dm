property _state : Object

Class constructor($state : Object)
	This._state:=$state

Function get done()->$done : Boolean
	$done:=This._state.done

Function get cancelled()->$cancelled : Boolean
	$cancelled:=This._state.cancelled

Function get result()->$result : Object
	If (This._state.done)
		$result:=JSON Parse(This._state.resultJSON)
	End if

Function cancel()->$accepted : Boolean
	Use (This._state)
		If (Not(This._state.done))
			If (Not(This._state.cancelled))
				This._state.cancelled:=True
				$accepted:=True
				If (This._state.worker>0)
					CALL WORKER(This._state.worker; Formula(_Overlay_cancel))
				End if
			End if
		End if
	End use
