// from RFC 1071
unsigned short int checksum(char *addr, unsigned int count)
{
  register unsigned int sum = 0;

  // Main summing loop
  while(count > 1)
  {
    sum = sum + *(unsigned short int *) addr++;
    count = count - 2;
  }

  // Add left-over byte, if any
  if (count > 0)
    sum = sum + *((char *) addr);

  // Fold 32-bit sum to 16 bits
  while (sum>>16)
    sum = (sum & 0xFFFF) + (sum >> 16);

  return(~sum);
}
