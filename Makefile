all: measure

measure: measure.c
	gcc measure.c -o measure

clean:
	rm -rf measure
