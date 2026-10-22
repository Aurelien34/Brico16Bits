@SET CONSOLEPORT=COM3
@SET ROMPORT=COM4
@SET ROMPATH=.\ROM\

@ECHO Ports %CONSOLEPORT% / %ROMPORT% configuration...
@mode %CONSOLEPORT%: dtr=on data=8 parity=n baud=57600 dtr=off rts=off> nul
@mode %ROMPORT%: data=8 parity=n baud=57600 dtr=off rts=off> nul

@echo:
@echo ROM UNIBIOS
@CALL :SENDROM UNIBIOS4.BIN 0

@echo:
@echo ROM NEODIAG
@CALL :SENDROM DIAG019.BIN 1

@echo:
@echo ROM NEOPENBIOS
@CALL :SENDROM NEOPEN.BIN 2

@GOTO :EOF

:SENDROM
@echo - Send
@copy /b %ROMPATH%%1 %ROMPORT% > nul
@REM Wait for BricoNeo to detect the end of transfer (300 ms without data) and process the image
@ping -n 2 127.0.0.1 > nul
@echo - Flash to bank %2
echo StoreBank %2 > %CONSOLEPORT%
@GOTO :EOF
