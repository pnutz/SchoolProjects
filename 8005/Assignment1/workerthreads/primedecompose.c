#include "primedecompose.h"
 
int decompose (mpz_t n, mpz_t *o) 
{
	int i = 0;
  	mpz_t tmp, d;
 
	mpz_init(tmp);
  	mpz_init(d);
 
  	while (mpz_cmp_si (n, 1)) 
	{
    		mpz_set_ui(d, 1);
    		do 
		{
      			mpz_add_ui(tmp, d, 1);
      			mpz_swap(tmp, d);
    		} while(!mpz_divisible_p(n, d));
    		mpz_divexact(tmp, n, d);
    		mpz_swap(tmp, n);
    		mpz_init(o[i]);
    		mpz_set(o[i], d);
    		i++;
  	}
  	return i;
}
