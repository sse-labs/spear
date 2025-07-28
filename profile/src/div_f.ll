@global = global float 0.0

define i32 @main() #0 {
  %1 = fdiv float 42.0, 3.0
  store volatile float %1, float* @global
  ret i32 0
}