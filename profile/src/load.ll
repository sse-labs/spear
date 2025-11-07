@global_src = global i8 17

define i32 @main() {
entry:
  %v = load volatile i8, i8* @global_src
  ret i32 0
}
