Test MACHINE a,b { 
    on WHEN b.pos > a.val;
    off DEFAULT;
}
Params MACHINE { OPTION val 100; }
Sampler MACHINE { OPTION pos 150; }

sampler Sampler(tab:test, pos:50);
params Params(tab:test, val:25);
test Test(tab:test) params,sampler;