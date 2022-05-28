/* Copy me if you can. */

#ifndef ARG_H__
#define ARG_H__

extern char *argv0;

#define ARGBEGIN {\
char *_arg, **_argp, **_args;\
for ((void)(argv0 = argv[0]), argc--, _args = _argp = ++argv; (_arg = *_argp); _argp++)\
	if (_arg[0] == '-' && _arg[1] == '-' && _arg[2] == '\0')\
		for (argc--, _argp++; (_arg = *_argp); _argp++)\
			*(_args++) = *_argp;\
	else if (_arg[0] == '-' && _arg[1] != '-' && _arg[1] != '\0')\
		for (argc--, _arg++; *_arg; (*_arg)? _arg++ : _arg)\
			switch (*_arg)

#define ARGLONG else if (_arg[0] == '-' && _arg[1] == '-' && _arg[2] != '\0')

#define ARGEND	else *(_args++) = *_argp; }

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

#endif /* ARG_H__ */
