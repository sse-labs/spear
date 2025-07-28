@global = global float 0.0

define i32 @main() #0 {
  %1 = fsub float 42.0, 311.0
  store volatile float %1, float* @global
  ret i32 0
}