/* Copy me if you can. */

#ifndef ARG_H
#define ARG_H

extern char *argv0;

#define ARGBEGIN {char *_arg, **_argp, **_args;				\
	for (argv0 = *argv++, argc--, _args = _argp = argv;		\
	     (_arg = *_argp) != NULL;	/* while != NULL */		\
	     *_argp ? _argp++ : 0)	/* inc only if _argp != NULL */	\
		if (*_arg == '-' && _arg[1] == '-' && _arg[2] == '\0')	\
			for (argc--, _argp++;	/* skip the '--' arg */	\
			     (_arg = *_argp) != NULL;			\
			     *_argp ? _argp++ : 0)			\
				*(_args++) = _arg; /* copy all args */	\
		else if (*_arg == '-' && _arg[1] != '-' && _arg[1] != '\0') \
			for (argc--, _arg++; *_arg; *_arg ? _arg++ : 0)	\
				switch (*_arg)

#define ARGLONG else if (*_arg == '-' && _arg[1] == '-' && _arg[2] != '\0')

#define ARGEND	else *(_args++) = _arg; /* else copy the argument */ }

#define ARGC()		*_arg
#define ARGF()		((_arg[1])? (*_arg = 0, _arg + 1) :\
			(_argp[1])? (argc--, _argp++, _argp[0]) :\
			NULL)
#define EARGF(x)	((_arg[1])? (*_arg = 0, _arg + 1) :\
			(_argp[1])? (argc--, _argp++, _argp[0]) :\
			((x), abort(), NULL))

#define ARGLC()		(_arg + 2)
#define ARGLF()		(_argp ? (_argp++, *_argp) : NULL)
#define EARGLF(x)	(_argp ? (_argp++, *_argp) : ((x), abort(), NULL))

#endif /* ARG_H */
