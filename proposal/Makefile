# LaTex Makefile

FILE=proposal

all: $(FILE).pdf

.PHONY: clean
clean:
	#\rm *.aux *.blg *.bbl *.log
	\rm *.aux *.log *.pdf

$(FILE).pdf: $(FILE).tex
	pdflatex $(FILE)
	pdflatex $(FILE)
	#bibtex $(FILE)
	pdflatex $(FILE)
	pdflatex $(FILE)
	pdflatex $(FILE)

read:
	evince $(FILE).pdf &
