@global = global i32 0

define i32 @main() {
entry:
  %0 = and i32 42, 311
  br i1 true, label %then, label %else

then:
  store volatile i32 %0, i32* @global
  ret i32 0

else:
  store volatile i32 %0, i32* @global
  ret i32 0
}
