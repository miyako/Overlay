#DECLARE($result : Object)->$diagnostic : Object
var $code; $message; $stderr; $previous; $canonical : Text
var $payload : Variant
var $parseErrors : Collection
var Overlay_Error_ParseErrors : Collection
$diagnostic:=New object("cancelled"; False; "nativeUsed"; False; "message"; "Overlay failed: missing result"; "result"; $result)
If ($result=Null)
	return
End if
$code:="unknown_error"
If (Value type($result.error)=Is object)
	If (Value type($result.error.code)=Is text)
		If ($result.error.code#"")
			$code:=$result.error.code
		End if
	End if
	If (Value type($result.error.message)=Is text)
		$message:=$result.error.message
	End if
End if
If (Value type($result.ok)=Is boolean)
	If (Not($result.ok))
		If (Value type($result.cancelled)=Is boolean)
			If ($result.cancelled & ($code="cancelled"))
				$diagnostic.cancelled:=True
			End if
		End if
	End if
End if
If (Value type($result.stderr)=Is text)
	$stderr:=$result.stderr
End if
$diagnostic.message:=$code
If ($message#"")
	$diagnostic.message+=": "+$message
End if
If ($stderr#"")
	$payload:=Null
	$previous:=Method called on error(ek local)
	Overlay_Error_ParseErrors:=Null
	ON ERR CALL("Overlay_error_capture"; ek local)
	$payload:=JSON Parse($stderr)
	ON ERR CALL($previous; ek local)
	$parseErrors:=Overlay_Error_ParseErrors
	Overlay_Error_ParseErrors:=Null
	$diagnostic.parseErrors:=$parseErrors
	If ($parseErrors=Null)
		If (Value type($payload)=Is object)
			If ((Value type($payload.protocol)=Is longint) | (Value type($payload.protocol)=Is real))
				If ($payload.protocol=1)
					If (Value type($payload.error)=Is object)
						If (Value type($payload.error.code)=Is text)
							If (Value type($payload.error.message)=Is text)
								If (($payload.error.code#"") & ($payload.error.message#""))
									$canonical:=JSON Stringify(New object("protocol"; 1; "error"; New object("code"; $payload.error.code; "message"; $payload.error.message)))+Char(10)
									If ($stderr=$canonical)
										$diagnostic.nativeUsed:=True
										$diagnostic.native:=$payload.error
										$diagnostic.message:=$payload.error.code+": "+$payload.error.message
										$diagnostic.message+=Char(10)+"Wrapper: "+$code
										If (($message#"") & ($message#$stderr))
											$diagnostic.message+=Char(10)+"Details: "+$message
										End if
									End if
								End if
							End if
						End if
					End if
				End if
			End if
		End if
	End if
	If (Not($diagnostic.nativeUsed))
		If ($stderr#$message)
			$diagnostic.message+=Char(10)+"Stderr: "+$stderr
		End if
	End if
End if
If ((Value type($result.exitCode)=Is longint) | (Value type($result.exitCode)=Is real))
	$diagnostic.message+=Char(10)+"Exit code: "+String($result.exitCode)
End if
