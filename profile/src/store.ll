@global_dst = global i8 0

define i32 @main() {
entry:
  store volatile i8 17, i8* @global_dst
  ret i32 0
}
