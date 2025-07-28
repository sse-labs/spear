@global = global i32 0

define i32 @main() #0 {
  %1 = mul i32 42, 3
  store volatile i32 %1, i32* @global
  ret i32 0
}