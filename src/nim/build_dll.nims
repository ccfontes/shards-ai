when defined build: # prevent nimsuggest execution...
  when defined windows:
    exec "nim cpp --newruntime --app:lib -d:forceCBRuntime -o:../py/chainblocks.dll -f chainblocks.nim"