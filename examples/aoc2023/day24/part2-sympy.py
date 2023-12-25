from itertools import combinations
import re
from sympy import Symbol, pprint, Matrix, Eq, solve, init_printing

init_printing()


t1 = Symbol('t1', real=True)
t2 = Symbol('t2', real=True)
t3 = Symbol('t3', real=True)
t4 = Symbol('t4', real=True)
t5 = Symbol('t5', real=True)
t_list = [t1,t2,t3,t4,t5]

d = [
    [int(x) for x in re.findall(r'-?\d+', line)]
    for line in open('sampleinput.txt').readlines()[:len(t_list)]
]

eqs = []

for i1, i2 in [(0,1), (0,2), (1,2)]:
    lines = [
        Matrix([x[i1], x[i2]]) + t*Matrix([x[3+i1], x[3+i2]])
        for x,t in zip(d,t_list)
    ]

    diffs = [v - w for v, w in combinations(lines, 2)]

    for diff1, diff2 in combinations(diffs, 2):
        eq = Eq(Matrix([list(diff1), list(diff2)]).det(), 0)
        pprint(eq)
        eqs.append(eq)

# It now contains terms like t1*t2.
# Turn it into a linear system of equations by replacing them with new variables named "t1t2" and so on.
# Surprisingly, the equations can still be solved after this.
mapping = {var1*var2: Symbol(str(var1) + str(var2), real=True) for var1, var2 in combinations(t_list, 2)}
eqs = [e.subs(mapping) for e in eqs]

for eq in eqs:
    pprint(eq)

pprint(solve(eqs))
