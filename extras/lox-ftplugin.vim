if exists("b:did_ftplugin")
    finish
endif
let b:did_ftplugin = 1

setlocal shiftwidth=2
setlocal tabstop=2
setlocal softtabstop=2
setlocal expandtab

setlocal cindent

let b:undo_ftplugin = "setlocal shiftwidth< tabstop< softtabstop< expandtab< cindent<"

" Set omnifunc for fallback or native LSP completion
setlocal omnifunc=lsp#complete

" Register the slox lsp server with vim-lsp if executable
if executable('slox')
    augroup SloxLspSetup
        autocmd!
        autocmd User lsp_setup call lsp#register_server({
                    \ 'name': 'slox-lsp',
                    \ 'cmd': {server_info->['slox', '/usr/lib64/slox/lib/lsp.lox']},
                    \ 'allowlist': ['lox'],
                    \ })
    augroup END
endif

" Configure keybindings when an LSP buffer attaches
function! s:on_lsp_buffer_init() abort
    nnoremap <buffer> <silent> gd :LspDefinition<CR>
    nnoremap <buffer> <silent> K  :LspHover<CR>
    nnoremap <buffer> <silent> <leader>n :LspRename<CR>
endfunction

augroup SloxLspBuffer
    autocmd!
    autocmd User lsp_buffer_enabled call s:on_lsp_buffer_init()
augroup END

nnoremap K :LspHover<CR>
