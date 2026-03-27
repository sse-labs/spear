Hash for loop for.cond

kind = loop_node
parent_function = main

llvm_loop_header =
  function = main
  block_index = 1
  instructions = [
    {
      opcode        = load
      result_type   = i32
      num_operands  = 1
      operands      = [ instruction_result:ptr ]
    },
    {
      opcode        = icmp
      result_type   = i1
      num_operands  = 2
      operands      = [ instruction_result:i32, const_int:1000 ]
    },
    {
      opcode        = br
      result_type   = void
      num_operands  = 3
      operands      = [ instruction_result:i1, basic_block:8, basic_block:2 ]
    }
  ]

has_sub_loops = 1
bounds = [1000, 1000]

nodes = [
  {
    id   = node0
    kind = basic_block_node

    hash =
      function = main
      block_index = 1
      instructions = [
        {
          opcode       = load
          result_type  = i32
          num_operands = 1
          operands     = [ instruction_result:ptr ]
        },
        {
          opcode       = icmp
          result_type  = i1
          num_operands = 2
          operands     = [ instruction_result:i32, const_int:1000 ]
        },
        {
          opcode       = br
          result_type  = void
          num_operands = 3
          operands     = [ instruction_result:i1, basic_block:8, basic_block:2 ]
        }
      ]
  },

  {
    id   = node1
    kind = basic_block_node

    hash =
      function = main
      block_index = 2
      instructions = [
        {
          opcode       = call
          result_type  = void
          num_operands = 4
          operands     = [ value:metadata, value:metadata, value:metadata, function:llvm.dbg.declare ]
          callee       = llvm.dbg.declare
        },
        {
          opcode       = store
          result_type  = void
          num_operands = 2
          operands     = [ const_int:0, instruction_result:ptr ]
        },
        {
          opcode       = br
          result_type  = void
          num_operands = 1
          operands     = [ basic_block:3 ]
        }
      ]
  },

  {
    id   = node2
    kind = basic_block_node

    hash =
      function = main
      block_index = 6
      instructions = [
        {
          opcode       = br
          result_type  = void
          num_operands = 1
          operands     = [ basic_block:7 ]
        }
      ]
  },

  {
    id   = node3
    kind = basic_block_node

    hash =
      function = main
      block_index = 7
      instructions = [
        {
          opcode       = load
          result_type  = i32
          num_operands = 1
          operands     = [ instruction_result:ptr ]
        },
        {
          opcode       = add
          result_type  = i32
          num_operands = 2
          operands     = [ instruction_result:i32, const_int:1 ]
        },
        {
          opcode       = store
          result_type  = void
          num_operands = 2
          operands     = [ instruction_result:i32, instruction_result:ptr ]
        },
        {
          opcode       = br
          result_type  = void
          num_operands = 1
          operands     = [ basic_block:1 ]
        }
      ]
  },

  {
    id   = node4
    kind = loop_node

    hash =
      kind = loop_node
      parent_function = main

      llvm_loop_header =
        function = main
        block_index = 3
        instructions = [
          {
            opcode       = load
            result_type  = i32
            num_operands = 1
            operands     = [ instruction_result:ptr ]
          },
          {
            opcode       = icmp
            result_type  = i1
            num_operands = 2
            operands     = [ instruction_result:i32, const_int:300 ]
          },
          {
            opcode       = br
            result_type  = void
            num_operands = 3
            operands     = [ instruction_result:i1, basic_block:6, basic_block:4 ]
          }
        ]

      has_sub_loops = 0
      bounds = [300, 300]

      nodes = [
        {
          id   = node0
          kind = basic_block_node

          hash =
            function = main
            block_index = 3
            instructions = [
              {
                opcode       = load
                result_type  = i32
                num_operands = 1
                operands     = [ instruction_result:ptr ]
              },
              {
                opcode       = icmp
                result_type  = i1
                num_operands = 2
                operands     = [ instruction_result:i32, const_int:300 ]
              },
              {
                opcode       = br
                result_type  = void
                num_operands = 3
                operands     = [ instruction_result:i1, basic_block:6, basic_block:4 ]
              }
            ]
        },

        {
          id   = node1
          kind = basic_block_node

          hash =
            function = main
            block_index = 4
            instructions = [
              {
                opcode       = load
                result_type  = i32
                num_operands = 1
                operands     = [ instruction_result:ptr ]
              },
              {
                opcode       = load
                result_type  = i32
                num_operands = 1
                operands     = [ instruction_result:ptr ]
              },
              {
                opcode       = mul
                result_type  = i32
                num_operands = 2
                operands     = [ instruction_result:i32, instruction_result:i32 ]
              },
              {
                opcode       = load
                result_type  = i32
                num_operands = 1
                operands     = [ instruction_result:ptr ]
              },
              {
                opcode       = add
                result_type  = i32
                num_operands = 2
                operands     = [ instruction_result:i32, instruction_result:i32 ]
              },
              {
                opcode       = store
                result_type  = void
                num_operands = 2
                operands     = [ instruction_result:i32, instruction_result:ptr ]
              },
              {
                opcode       = br
                result_type  = void
                num_operands = 1
                operands     = [ basic_block:5 ]
              }
            ]
        },

        {
          id   = node2
          kind = basic_block_node

          hash =
            function = main
            block_index = 5
            instructions = [
              {
                opcode       = load
                result_type  = i32
                num_operands = 1
                operands     = [ instruction_result:ptr ]
              },
              {
                opcode       = add
                result_type  = i32
                num_operands = 2
                operands     = [ instruction_result:i32, const_int:1 ]
              },
              {
                opcode       = store
                result_type  = void
                num_operands = 2
                operands     = [ instruction_result:i32, instruction_result:ptr ]
              },
              {
                opcode       = br
                result_type  = void
                num_operands = 1
                operands     = [ basic_block:3 ]
              }
            ]
        },

        {
          id   = node3
          kind = virtual_node

          hash =
            kind = virtual_node
            is_entry = 0
            is_exit = 1
            parent_kind = loop_node
        },

        {
          id   = node4
          kind = virtual_node

          hash =
            kind = virtual_node
            is_entry = 1
            is_exit = 0
            parent_kind = loop_node
        }
      ]

      edges = [
        node0 -> node1 (feasible = 1),
        node1 -> node2 (feasible = 1),
        node2 -> node0 (feasible = 1, backedge = 1),
        node2 -> node3 (feasible = 1),
        node4 -> node0 (feasible = 1)
      ]

      explicit_backedge = node2 -> node0
  },

  {
    id   = node5
    kind = virtual_node

    hash =
      kind = virtual_node
      is_entry = 0
      is_exit = 1
      parent_kind = loop_node
  },

  {
    id   = node6
    kind = virtual_node

    hash =
      kind = virtual_node
      is_entry = 1
      is_exit = 0
      parent_kind = loop_node
  }
]

edges = [
  node0 -> node1 (feasible = 1),
  node1 -> node4 (feasible = 1),
  node2 -> node3 (feasible = 1),
  node3 -> node0 (feasible = 1, backedge = 1),
  node3 -> node5 (feasible = 1),
  node4 -> node2 (feasible = 1),
  node6 -> node0 (feasible = 1)
]

explicit_backedge = node3 -> node0