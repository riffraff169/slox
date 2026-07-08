if exists("b:current_syntax")
    finish
endif

runtime! syntax/c.vim

syn keyword cStatement fun var class super print require include class import try catch finally throw const
syn keyword cConstant nil true false

let b:current_syntax = "lox"
