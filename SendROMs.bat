@SET COMPORT=COM5
@SET ROMPATH=.\ROM\

@ECHO Port %COMPORT% configuration...
@mode %COMPORT%: dtr=on data=8 parity=n baud=57600 dtr=off rts=off> nul

@echo:
@echo ROM UNIBIOS
@echo SendROM> %COMPORT%
@echo - Send
@type %ROMPATH%UNIBIOS4.BIN> %COMPORT%
@echo - Flash to bank 0
@echo StoreBank 0 > %COMPORT%

@echo:
@echo ROM NEODIAG
@echo SendROM> %COMPORT%
@echo - Send
@type %ROMPATH%DIAG019.BIN> %COMPORT%
@echo - Flash to bank 1
@echo StoreBank 1 > %COMPORT%

@echo:
@echo ROM NEOPENBIOS
@echo SendROM> %COMPORT%
@echo - Send
@type %ROMPATH%NEOPEN.BIN> %COMPORT%
@echo - Flash to bank 2
@echo StoreBank 2 > %COMPORT%
