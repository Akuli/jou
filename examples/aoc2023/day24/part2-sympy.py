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

lines = [
    Matrix(x[:3]) + t*Matrix(x[3:])
    for x,t in zip(d,t_list)
]

diffs = [v - w for v, w in combinations(lines, 2)]

projection_matrices = [
    Matrix([
        [1, 0, 0],
        [0, 1, 0],
    ]),
    Matrix([
        [1, 0, 0],
        [0, 0, 1],
    ]),
    Matrix([
        [0, 1, 0],
        [0, 0, 1],
    ]),
]

eqs = []
for M in projection_matrices:
    for diff1, diff2 in combinations(diffs, 2):
        assert Matrix([list(M*diff1), list(M*diff2)]).transpose() == M*Matrix([list(diff1),list(diff2)]).transpose()
        eq = Eq(Matrix([list(M*diff1), list(M*diff2)]).det(), 0)
        pprint(eq)
        eqs.append(eq)

for eq in eqs:
    pprint(eq)

pprint(solve(eqs, t_list))
