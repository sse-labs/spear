@global = global i32 0
@res1 = global i32 0
@res2 = global i32 0
@res3 = global i32 0
@res4 = global i32 0

define i32 @main() #0 {
  %1 = add i32 3, 7
  %2 = sub i32 3, 7
  %3 = mul i32 3, 7
  %4 = udiv i32 3, 7
  store volatile i32 %1, i32* @global
  store volatile i32 %2, i32* @res1
  store volatile i32 %3, i32* @res2
  store volatile i32 %4, i32* @res3
  ret i32 0
}