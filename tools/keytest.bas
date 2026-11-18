100 REMark QL keyboard test
110 MODE 4: PAPER #0,0: CLS #0: PAPER #2,0: CLS #2: PAPER 0: CLS: WINDOW 448,200,32,16: INK 7: CLS
120 DIM n$(63,6), p%(7)
130 RESTORE 1000: FOR i=0 TO 63: READ n$(i)
140 FOR b=0 TO 7: p%(b)=2^b
150 CLS: PRINT "QL keyboard test": PRINT
160 PRINT "1  Characters: each key's code (INKEY$)"
170 PRINT "2  Matrix: the keys held now (KEYROW)"
180 PRINT: PRINT "CTRL+SPACE stops a test, RUN brings this back"
190 k$=INKEY$(-1)
200 IF k$="1" THEN chars
210 IF k$="2" THEN matrix
220 GO TO 190
230 DEFine PROCedure chars
240   CLS: PRINT "Characters (CTRL+SPACE to stop)": PRINT
250   REPeat c
260     k$=INKEY$(-1): k=CODE(k$)
270     PRINT k;
280     IF k>31 AND k<192 THEN PRINT "  ";k$;
290     PRINT
300   END REPeat c
310 END DEFine
320 DEFine PROCedure matrix
330   CLS: PRINT "Keys held (CTRL+SPACE to stop)": o$="?"
340   REPeat m
350     h$=""
360     FOR r=7 TO 0 STEP -1
370       v%=KEYROW(r)
380       FOR b=0 TO 7
390         IF v% && p%(b) THEN h$=h$&n$((7-r)*8+b)&" "
400       END FOR b
410     END FOR r
420     IF h$<>o$ THEN AT 3,0: CLS 3: CLS 2: PRINT h$: o$=h$
430   END REPeat m
440 END DEFine
1000 DATA "SHIFT","CTRL","ALT","X","V","/","N",","
1010 DATA "8","2","6","Q","E","0","T","U"
1020 DATA "9","W","I","TAB","R","-","Y","O"
1030 DATA "L","3","H","1","A","P","D","J"
1040 DATA "[","CAPS","K","S","F","=","G",";"
1050 DATA "]","Z",".","C","B","POUND","M","'"
1060 DATA "ENTER","LEFT","UP","ESC","RIGHT","\","SPACE","DOWN"
1070 DATA "F4","F1","5","F2","F3","F5","4","7"
