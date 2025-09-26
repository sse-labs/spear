@global = global i1 0

define i32 @main() #0 {
  %1 = icmp sge i32 252, 42
  store volatile i1 %1, i1* @global
  ret i32 0
}