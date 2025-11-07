@array = global [4 x i8] zeroinitializer
@ptr_sink = global i8* null   ; where we store the computed pointer

define i32 @main() {
entry:
  ; Compute address: &array[2]
  %ptr = getelementptr [4 x i8], [4 x i8]* @array, i32 0, i32 2

  ; Prevent optimization by storing pointer value
  store volatile i8* %ptr, i8** @ptr_sink

  ret i32 0
}
