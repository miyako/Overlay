property _cli : cs._CLI

Class constructor($target : Text)
	This._cli:=cs._CLI.new("overlay-cli"; $target)

Function version()->$result : Object
	$result:=This._decode(This._cli.execute(New collection("--version")); "version")

Function screens()->$result : Object
	$result:=This._decode(This._cli.execute(New collection("screens")); "screens")

Function capabilities()->$result : Object
	$result:=This._decode(This._cli.execute(New collection("capabilities")); "capabilities")

Function show($options : Object; $callback : Object)->$handle : cs.OverlayHandle
	var $empty : Blob
	$handle:=This._start($options; $callback; False; $empty)

Function showBlob($png : Blob; $options : Object; $callback : Object)->$handle : cs.OverlayHandle
	$handle:=This._start($options; $callback; True; $png)

Function _start($options : Object; $callback : Object; $stdin : Boolean; $png : Blob)->$handle : cs.OverlayHandle
	var $prepared; $resolved; $state : Object
	$state:=New shared object("done"; False; "cancelled"; False; "worker"; 0; "resultJSON"; "")
	$handle:=cs.OverlayHandle.new($state)
	$prepared:=This._arguments($options; $stdin)
	If ($stdin)
		If (BLOB size($png)>33554432)
			$prepared:=This._failure("invalid_argument"; "PNG Blob exceeds 32 MiB")
		End if
	End if
	If ($callback#Null)
		If (Not(OB Instance of($callback; 4D.Function)))
			If (Value type($callback.formula)#Is object)
				$prepared:=This._failure("invalid_argument"; "Callback must be a Formula or formula/context Object")
				$callback:=Null
			Else
				If (Not(OB Instance of($callback.formula; 4D.Function)))
					$prepared:=This._failure("invalid_argument"; "Callback formula must be a Formula")
					$callback:=Null
				Else
					If ($callback.context#Null)
						If (Value type($callback.context)#Is object)
							$prepared:=This._failure("invalid_argument"; "Callback context must be an Object or Null")
							$callback:=Null
						End if
					End if
				End if
			End if
		End if
	End if
	If ($prepared.ok)
		$resolved:=This._cli.resolve()
		If ($resolved.ok)
			$prepared:=cs._Process.new().commandLine($resolved.file; $prepared.arguments)
		Else
			$prepared:=$resolved
		End if
	End if
	CALL WORKER("Overlay-"+Generate UUID; Formula(_Overlay_start($1; $2; $3; $4; $5; $6)); $state; $prepared; $callback; $stdin; $png; $options)

Function _arguments($options : Object; $stdin : Boolean)->$result : Object
	var $arguments : Collection
	var $key; $flag : Text
	var $value : Variant
	If ($options=Null)
		return This._failure("invalid_argument"; "Options object is required")
	End if
	$arguments:=New collection("show")
	If ($stdin)
		If ($options.image#Null)
			return This._failure("invalid_argument"; "showBlob does not accept image")
		End if
		$arguments.push("--stdin")
	Else
		If ($options.image=Null)
			return This._failure("invalid_argument"; "Image Text or File is required")
		End if
		$value:=$options.image
		If (Value type($value)#Is text)
			If (Value type($value)#Is object)
				return This._failure("invalid_argument"; "Image must be Text or File")
			End if
			If (Not(OB Instance of($value; 4D.File)))
				return This._failure("invalid_argument"; "Image must be Text or File")
			End if
		End if
		$arguments.push("--image"; $value)
	End if
	For each ($key; $options)
		$value:=$options[$key]
		$flag:=""
		Case of
			: ($key="image")
				// Already validated separately from scalar CLI options.
			: ($key="mode")
				If (Value type($value)#Is text)
					return This._failure("invalid_argument"; "Mode must be Text")
				End if
				$arguments.push("--mode"; $value)
			: ($key="modal")
				If (Value type($value)#Is boolean)
					return This._failure("invalid_argument"; "Modal must be Boolean")
				End if
				$arguments.push("--modal"; JSON Stringify($value))
			Else
				Case of
					: ($key="x")
						$flag:="--x"
					: ($key="y")
						$flag:="--y"
					: ($key="timeoutMs")
						$flag:="--timeout-ms"
					: ($key="fadeInMs")
						$flag:="--fade-in-ms"
					: ($key="fadeOutMs")
						$flag:="--fade-out-ms"
					: ($key="opacity")
						$flag:="--opacity"
					: ($key="scale")
						$flag:="--scale"
					: ($key="screen")
						$flag:="--screen"
					: ($key="hostPid")
						$flag:="--host-pid"
				End case
				If ($flag="")
					return This._failure("invalid_argument"; "Unknown overlay option: "+$key)
				End if
				If (Not(This._number($value)))
					return This._failure("invalid_argument"; "Numeric option required: "+$key)
				End if
				$arguments.push($flag; JSON Stringify($value))
		End case
	End for each
	If ($options.x=Null)
		$arguments.push("--x"; "0")
	End if
	If ($options.y=Null)
		$arguments.push("--y"; "0")
	End if
	$result:=New object("ok"; True; "arguments"; $arguments)

Function _number($value : Variant)->$valid : Boolean
	$valid:=(Value type($value)=Is real) | (Value type($value)=Is longint)

Function _failure($code : Text; $message : Text)->$result : Object
	$result:=New object("ok"; False; "error"; New object("code"; $code; "message"; $message))

Function _decode($result : Object; $kind : Text)->$decoded : Object
	var $payload; $entry : Variant
	var $previous; $field : Text
	var $parseErrors : Collection
	var CLI_LastErrors : Collection
	If (Not($result.ok))
		return $result
	End if
	$previous:=Method called on error(ek local)
	CLI_LastErrors:=Null
	ON ERR CALL("_CaptureError"; ek local)
	$payload:=JSON Parse($result.stdout)
	ON ERR CALL($previous; ek local)
	$parseErrors:=CLI_LastErrors
	CLI_LastErrors:=Null
	If (($parseErrors#Null) | (Value type($payload)#Is object))
		return This._invalid($result; "CLI response must be a JSON object")
	End if
	If (Not(This._number($payload.protocol)))
		return This._invalid($result; "Unsupported CLI protocol version")
	End if
	If ($payload.protocol#1)
		return This._invalid($result; "Unsupported CLI protocol version")
	End if
	Case of
		: ($kind="version")
			If (Value type($payload.version)#Is text)
				return This._invalid($result; "Version must be Text")
			End if
		: ($kind="capabilities")
			If (Value type($payload.modal)#Is boolean)
				return This._invalid($result; "Modal capability must be Boolean")
			End if
			If (Value type($payload.format)#Is text)
				return This._invalid($result; "Image format capability must be Text")
			End if
			If (Value type($payload.modes)#Is collection)
				return This._invalid($result; "Modes capability must be a Collection")
			End if
			If ($payload.modes.length#2)
				return This._invalid($result; "Unexpected supported modes")
			End if
			If ((Value type($payload.modes[0])#Is text) | (Value type($payload.modes[1])#Is text))
				return This._invalid($result; "Supported modes must be Text")
			End if
			If ($payload.modal | ($payload.format#"png") | ($payload.modes[0]#"passive") | ($payload.modes[1]#"clickable"))
				return This._invalid($result; "Unsupported native capabilities")
			End if
		: ($kind="screens")
			If (Value type($payload.screens)#Is collection)
				return This._invalid($result; "Screens must be a Collection")
			End if
			For each ($entry; $payload.screens)
				If (Value type($entry)#Is object)
					return This._invalid($result; "Screen must be an Object")
				End if
				For each ($field; New collection("index"; "x"; "y"; "width"; "height"; "scale"))
					If (Not(This._number($entry[$field])))
						return This._invalid($result; "Screen coordinates must be numeric")
					End if
				End for each
				If (($entry.width<=0) | ($entry.height<=0) | ($entry.scale<=0) | ($entry.index<0))
					return This._invalid($result; "Invalid screen geometry")
				End if
			End for each
		: ($kind="show")
			If (Value type($payload.outcome)#Is text)
				return This._invalid($result; "Outcome must be Text")
			End if
			If (Value type($payload.reason)#Is text)
				return This._invalid($result; "Reason must be Text")
			End if
			Case of
				: ($payload.outcome="accepted")
					If ($payload.reason#"click")
						return This._invalid($result; "Accepted outcome requires click reason")
					End if
					If (Not(This._number($payload.x)))
						return This._invalid($result; "X must be numeric")
					End if
					If (Not(This._number($payload.y)))
						return This._invalid($result; "Y must be numeric")
					End if
					If (Not(($payload.x>=0) & ($payload.x<=8192)))
						return This._invalid($result; "X must be finite and within local rendered bounds")
					End if
					If (Not(($payload.y>=0) & ($payload.y<=8192)))
						return This._invalid($result; "Y must be finite and within local rendered bounds")
					End if
				: ($payload.outcome="dismissed")
					If (New collection("timeout"; "escape"; "cancel"; "host-exit"; "screen-disconnect").indexOf($payload.reason)<0)
						return This._invalid($result; "Unknown dismissal reason")
					End if
					If (OB Is defined($payload; "x") | OB Is defined($payload; "y"))
						return This._invalid($result; "Dismissed outcome must not contain coordinates")
					End if
				Else
					return This._invalid($result; "Unknown terminal outcome")
			End case
			If (Not(This._terminalRecord($result.stdout; $payload.outcome)))
				return This._invalid($result; "Expected exactly one compact terminal JSON record followed by LF")
			End if
		Else
			return This._invalid($result; "Unknown response kind")
	End case
	$result.value:=$payload
	$decoded:=$result

Function _terminalRecord($stdout : Text; $outcome : Text)->$valid : Boolean
	var $body; $number; $pattern : Text
	If (Length($stdout)<2)
		return False
	End if
	If (Substring($stdout; Length($stdout); 1)#Char(10))
		return False
	End if
	$body:=Substring($stdout; 1; Length($stdout)-1)
	If ((Position(Char(10); $body; *)>0) | (Position(Char(13); $body; *)>0))
		return False
	End if
	$number:="-?(0|[1-9][0-9]*)(\\.[0-9]+)?([eE][+-]?[0-9]+)?"
	If ($outcome="accepted")
		$pattern:="^\\{\"protocol\":1,\"outcome\":\"accepted\",\"reason\":\"click\",\"x\":"+$number+",\"y\":"+$number+"\\}$"
	Else
		$pattern:="^\\{\"protocol\":1,\"outcome\":\"dismissed\",\"reason\":\"(timeout|escape|cancel|host-exit|screen-disconnect)\"\\}$"
	End if
	$valid:=Match regex($pattern; $body)

Function _invalid($result : Object; $message : Text)->$invalid : Object
	$result.ok:=False
	OB REMOVE($result; "value")
	$result.error:=New object("code"; "invalid_protocol"; "message"; $message)
	$invalid:=$result
