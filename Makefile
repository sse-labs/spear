
lint:
	pipx run cpplint --recursive --linelength=120 src/spear lib/ main.cpp

printcfg:
	@opt \
      --passes='function(instnamer,mem2reg,loop-simplify,lcssa,loop(loop-rotate,indvars)),dot-cfg' \
      ../programs/examples/compiled/arrayReducer_complex.ll \
      -disable-output


ARG := $(word 2,$(MAKECMDGOALS))

# Prevent make from treating the path as a target
$(ARG):
	@: