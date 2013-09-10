SHELL=/bin/bash

FIGS = jpip_fig 	\
	partition	\
	codestream	\
	architecture	\
	client_manager
	
%.eps %.tex: %.fig
				fig2dev -L pstex $*.fig > $*.eps
				(fig2dev -L pstex_t -p $* $< | sed 's/includegraphics{/includegraphics{..\//g') > $*.tex

%.pdf: %.eps
				epstopdf $*.eps
	
doc: $(FIGS:%=%.pdf) documentation

clean:
	rm -rf *.pdf latex html doxyfile.ok
	rm -rf $(FIGS:%=%.pdf) $(FIGS:%=%.tex) $(FIGS:%=%.eps)

documentation:
	n=$$(cd ..; pwd) &&  			\
	cat doxyfile | 			\
		sed -e "s:__SRC_DIR__:$$n/src/:g" | 	\
	 	sed -e "s:__DOC_DIR__:$$n/doc/:g" 	\
	 	> doxyfile.ok &&			\
	doxygen doxyfile.ok &&		\
	make -C latex &&			\
	mv latex/refman.pdf .
