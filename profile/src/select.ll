@global = global i8 0

define i32 @main() #0 {
  %1 = select i1 true, i8 17, i8 42
  store volatile i8 %1, i8* @global
  ret i32 0
}