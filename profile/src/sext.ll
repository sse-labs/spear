@global = global i64 0

define i32 @main() #0 {
  %1 = sext i32 257 to i64
  store volatile i64 %1, i64* @global
  ret i32 0
}