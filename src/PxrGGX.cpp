/*  $Date: 2015/07/06 $  $Revision: #2 $
# ------------------------------------------------------------------------------
#
# Copyright (c) 2014 Pixar Animation Studios. All rights reserved.
#
# The information in this file (the "Software") is provided for the
# exclusive use of the software licensees of Pixar.  Licensees have
# the right to incorporate the Software into other products for use
# by other authorized software licensees of Pixar, without fee.
# Except as expressly permitted herein, the Software may not be
# disclosed to third parties, copied or duplicated in any form, in
# whole or in part, without the prior written permission of
# Pixar Animation Studios.
#
# The copyright notices in the Software and this entire statement,
# including the above license grant, this restriction and the
# following disclaimer, must be included in all copies of the
# Software, in whole or in part, and all permitted derivative works of
# the Software, unless such copies or derivative works are solely
# in the form of machine-executable object code generated by a
# source language processor.
#
# PIXAR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
# ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT
# SHALL PIXAR BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES
# OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
# WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
# ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
# SOFTWARE.
#
# Pixar
# 1200 Park Ave
# Emeryville CA 94608
#
# ------------------------------------------------------------------------------
*/
#include "RixBxdf.h"
#include "RixShadingUtils.h"
#include <cstring> // memset

#define k_minRoughness .001f

static const RtFloat k_minfacing = .0001f; // NdV < k_minfacing is invalid

static const unsigned char k_reflBlinnLobeId = 0;

static RixBXLobeSampled s_reflBlinnLobe;

static RixBXLobeTraits s_reflBlinnLobeTraits;

class PxrGGX : public RixBsdf
{
public:

    PxrGGX(RixShadingContext const *sc, RixBxdfFactory *bx,
               RixBXLobeTraits const &lobesWanted,
               RtColorRGB const *blinn, RtFloat const *rough) :
        RixBsdf(sc, bx),
        m_lobesWanted(lobesWanted),
        m_colour(blinn),
        m_rough(rough)
    {
        RixBXLobeTraits lobes = s_reflBlinnLobeTraits;

        m_lobesWanted &= lobes;

        sc->GetBuiltinVar(RixShadingContext::k_P, &m_P);
        sc->GetBuiltinVar(RixShadingContext::k_Nn, &m_Nn);
        sc->GetBuiltinVar(RixShadingContext::k_Ngn, &m_Ngn);
        sc->GetBuiltinVar(RixShadingContext::k_Tn, &m_Tn);
        sc->GetBuiltinVar(RixShadingContext::k_Vn, &m_Vn);

    }

    virtual RixBXEvaluateDomain GetEvaluateDomain()
    {
        return k_RixBXFront;  // two-sided, but opaque
    }
    virtual void GetAggregateLobeTraits(RixBXLobeTraits *t)
    {
        *t = m_lobesWanted;
    }

    virtual void GenerateSample(RixBXTransportTrait transportTrait,
                                RixBXLobeTraits const *lobesWanted,
                                RixRNG *rng,
                                RixBXLobeSampled *lobeSampled,
                                RtVector3   *Ln,
                                RixBXLobeWeights &W,
                                RtFloat *FPdf, RtFloat *RPdf)
    {
        RtInt nPts = shadingCtx->numPts;
        RixBXLobeTraits all = GetAllLobeTraits();
        RtFloat2 *xi = (RtFloat2 *) RixAlloca(sizeof(RtFloat2) * nPts);
        rng->DrawSamples2D(nPts,xi);

        RtColorRGB *reflDiffuseWgt = NULL;

        RtNormal3 Nf;

        for(int i = 0; i < nPts; i++)
        {
            lobeSampled[i].SetValid(false);

            RixBXLobeTraits lobes = (all & lobesWanted[i]);
            bool doDiff = (lobes & s_reflBlinnLobeTraits).HasAny();

            if (!reflDiffuseWgt && doDiff)
                reflDiffuseWgt = W.AddActiveLobe(s_reflBlinnLobe);
            if (doDiff)
            {
                // we generate samples on the (front) side of Vn since
                // we have no translucence effects.
                RtFloat NdV;
                NdV = m_Nn[i].Dot(m_Vn[i]);
                if(NdV >= 0.f)
                {
                    Nf = m_Nn[i];
                }
                else
                {
                    Nf = -m_Nn[i];
                    NdV = -NdV;
                }
                if(NdV > k_minfacing)
                {
                    generate(NdV, Nf, m_Tn[i], m_colour[i],m_rough[i], xi[i],
                             Ln[i],m_Vn[i], reflDiffuseWgt[i], FPdf[i], RPdf[i]);
                    lobeSampled[i] = s_reflBlinnLobe;
                }
                // else invalid.. NullTrait
            }
        }

    }

    virtual void EvaluateSample(RixBXTransportTrait transportTrait,
                                RixBXLobeTraits const *lobesWanted,
                                RixBXLobeTraits *lobesEvaluated,
                                RtVector3 const *Ln, RixBXLobeWeights &W,
                                RtFloat *FPdf, RtFloat *RPdf)
    {
        RtNormal3 Nf;
        RtInt nPts = shadingCtx->numPts;
        RixBXLobeTraits all = GetAllLobeTraits();

        RtColorRGB *reflDiffuseWgt = NULL;

        for(int i = 0; i < nPts; i++)
        {
            lobesEvaluated[i].SetNone();
            RixBXLobeTraits lobes = (all & lobesWanted[i]);
            bool doDiff = (lobes & s_reflBlinnLobeTraits).HasAny();

            if (!reflDiffuseWgt && doDiff)
                reflDiffuseWgt = W.AddActiveLobe(s_reflBlinnLobe);

            if (doDiff)
            {
                RtFloat NdV;
                NdV = m_Nn[i].Dot(m_Vn[i]);
                if(NdV >= 0.f)
                    Nf = m_Nn[i];
                else
                {
                    Nf = -m_Nn[i];
                    NdV = -NdV;
                }
                if(NdV > k_minfacing)
                {
                    RtFloat NdL = Nf.Dot(Ln[i]);
                    if(NdL > 0.f)
                    {
                        evaluate(NdV, NdL,Nf, m_colour[i],m_rough[i],Ln[i],m_Vn[i],
                                 reflDiffuseWgt[i], FPdf[i], RPdf[i]);
                        lobesEvaluated[i] |= s_reflBlinnLobeTraits;
                    }
                }
            }
        }


    }

    virtual void EvaluateSamplesAtIndex(RixBXTransportTrait transportTrait,
                                        RixBXLobeTraits const &lobesWanted,
                                        RtInt index, RtInt nsamps,
                                        RixBXLobeTraits *lobesEvaluated,
                                        RtVector3 const *Ln,
                                        RixBXLobeWeights &W,
                                        RtFloat *FPdf, RtFloat *RPdf)
    {
        for (int i = 0; i < nsamps; i++)
            lobesEvaluated[i].SetNone();

        RixBXLobeTraits lobes = lobesWanted & GetAllLobeTraits();
        bool doDiff = (lobes & s_reflBlinnLobeTraits).HasAny();

        if(!doDiff)
            return;

        RtNormal3 const &Nn = m_Nn[index];
        RtNormal3 const &Ngn = m_Ngn[index];
        RtVector3 const &Vn = m_Vn[index];
        RtColorRGB const &diff = m_colour[index];
        RtFloat const &rough = m_rough[index];


        // Make any lobes that we may evaluate or write to active lobes,
        // initialize their lobe weights to zero and fetch a pointer to the
        // lobe weight arrays.

        RtColorRGB *reflDiffuseWgt = doDiff
            ? W.AddActiveLobe(s_reflBlinnLobe) : NULL;

        RtNormal3 Nf;
        RtFloat NdV;

        NdV = Nn.Dot(Vn);
        if(NdV >= .0f)
            Nf = Nn;
        else
        {
            Nf = -Nn;
            NdV = -NdV;
        }
        RtFloat NfdV;
        NfdV = NdV;
        if(NdV > k_minfacing)
        {
            for(int i=0; i<nsamps; ++i)
            {
                RtFloat NdL = Nf.Dot(Ln[i]);
                if(NdL > 0.f)
                {
                    evaluate(NfdV, NdL,Nf, diff,rough,Ln[index],Vn,
                             reflDiffuseWgt[i], FPdf[i], RPdf[i]);
                    lobesEvaluated[i] |= s_reflBlinnLobeTraits;
                }
            }
        }
    }


private:


    PRMAN_INLINE
    void generate(RtFloat NdV,
                 const RtNormal3 &Nn, const RtVector3 &Tn,
                 const RtColorRGB &color,
                 const RtFloat &rough,
                 const RtFloat2 &xi,
                 RtVector3 &Ln, RtVector3 const &inDir, RtColorRGB  &W,
                 RtFloat &FPdf, RtFloat &RPdf)
    {
        float r = fmax(k_minRoughness,rough);
        float radiance = 0.f;
        float pdf = 1.f;
        RtVector3 TX, TY;
        RixComputeShadingBasis(Nn, Tn, TX, TY);

        float alpha = r*r;
        float alphaSqrd = alpha*alpha;
        // Compute our new direction
        // half angle
        float cosTheta = sqrtf((1.f-xi.x)/(xi.x*(alphaSqrd-1.f)+1.f));
        float cosThetaSqrd = cosTheta*cosTheta;
        float sinTheta = sqrtf(fmax(0.f,1.f-cosThetaSqrd));
        float phi = xi.y * 2.f * M_PI;
        float x = sinTheta*cosf(phi);
        float y = sinTheta*sinf(phi);
        float z = cosTheta;
        RtVector3 m = x * TX + y * TY + z * Nn;
        // scattered light direction
        Ln = 2.f*inDir.AbsDot(m)*m-inDir;
        // pdf
        pdf = (alphaSqrd*cosTheta*sinTheta)/(M_PI*((alphaSqrd-1.f)*cosThetaSqrd+1.f)*((alphaSqrd-1.f)*cosThetaSqrd+1.f));

        //GGX NDF
        float D;
        if(cosTheta<=0.f)
            D = 0.f;
        else
            D = (alphaSqrd*cosTheta)/(M_PI*((alphaSqrd-1.f)*cosThetaSqrd+1.f)*((alphaSqrd-1.f)*cosThetaSqrd+1.f));
                // Smiths shadow function
        float IdN = Nn.Dot(inDir);
        float OdN = Nn.Dot(Ln);
        float G1,G2;
        if(IdN<=0.f)
            G1 = 0.f;
        else
            G1 = 2.f/(1.f+sqrtf(alphaSqrd*(1.f-IdN*IdN)/(IdN*IdN)));
        if(OdN<=0.f)
            G2 = 0.f;
        else
            G2 = 2.f/(1.f+sqrtf(alphaSqrd*(1.f-OdN*OdN)/(OdN*OdN)));

        // Radiance value
        radiance = (G1*G2*D)/(4.f*IdN);

        W = color * radiance;
        FPdf = D * G1 / (4.f * IdN);
        RPdf = D * G2 / (4.f * OdN);

    }

    PRMAN_INLINE
    void evaluate(RtFloat NdV, RtFloat NdL, RtNormal3 &Nn, const RtColorRGB &color, const RtFloat &rough,
                 RtVector3 Ln, RtVector3 const &inDir,
                 RtColorRGB  &W, RtFloat &FPdf, RtFloat &RPdf)
    {

        //Compute our blinn weighting and PDF
        float radiance = 0.f;
        float pdf = 1.f;

        float r = fmax(k_minRoughness,rough);
        //PDF
        RtVector3 m = Ln + inDir;
        m.Normalize();
        float alpha = r*r;
        float alphaSqrd = alpha*alpha;
        float cosTheta = fabs(m.Dot(Nn));
        float cosThetaSqrd = cosTheta*cosTheta;
        float sinTheta = sqrtf(fmax(0.f,1.f-cosThetaSqrd));
        pdf = (alphaSqrd*cosTheta*sinTheta)/(M_PI*((alphaSqrd-1.f)*cosThetaSqrd+1.f)*((alphaSqrd-1.f)*cosThetaSqrd+1.f));

        //GGX NDF
        float D;
        if(cosTheta<=0.f)
            D = 0.f;
        else
            D = (alphaSqrd*cosTheta)/(M_PI*((alphaSqrd-1.f)*cosThetaSqrd+1.f)*((alphaSqrd-1.f)*cosThetaSqrd+1.f));
        // Smiths shadow function
        float IdN = Nn.Dot(inDir);
        float OdN = Nn.Dot(Ln);
        float G1,G2;
        if(IdN<=0.f)
            G1 = 0.f;
        else
            G1 = 2.f/(1.f+sqrtf(alphaSqrd*(1.f-IdN*IdN)/(IdN*IdN)));
        if(OdN<=0.f)
            G2 = 0.f;
        else
            G2 = 2.f/(1.f+sqrtf(alphaSqrd*(1.f-OdN*OdN)/(OdN*OdN)));

        // Radiance value
        radiance = (G1*G2*D)/(4.f*IdN);

        W = color * radiance;
        FPdf = D * G1 / (4.f * IdN);
        RPdf = D * G2 / (4.f * OdN);

    }
private:
    RixBXLobeTraits m_lobesWanted;
    RtColorRGB const *m_colour;
    RtFloat const *m_rough;
    RtPoint3 const* m_P;
    RtVector3 const* m_Vn;
    RtVector3 const* m_Tn;
    RtNormal3 const* m_Nn;
    RtNormal3 const* m_Ngn;
};

// PxrGGXFactory Implementation
class PxrGGXFactory : public RixBxdfFactory
{
public:


    PxrGGXFactory();
    ~PxrGGXFactory();

    virtual int Init(RixContext &, char const *pluginpath);
    RixSCParamInfo const *GetParamTable();
    virtual void Finalize(RixContext &);

    virtual void Synchronize(RixContext &ctx, RixSCSyncMsg syncMsg,
                             RixParameterList const *parameterList);

    virtual int CreateInstanceData(RixContext &,
                                   char const *handle,
                                   RixParameterList const *,
                                   InstanceData *id);

    virtual int GetInstanceHints(RtConstPointer instanceData) const;

    virtual RixBsdf *BeginScatter(RixShadingContext const *,
                                  RixBXLobeTraits const &lobesWanted,
                                  RixSCShadingMode sm,
                                  RtConstPointer instanceData);
    virtual void EndScatter(RixBsdf *);

  private:
    // these hold the default (def) values
    //----------------------------------------------------------------------------------------------------------------------
    /// @brief Defualt colour of our GGX BRDF
    //----------------------------------------------------------------------------------------------------------------------
    RtColorRGB m_colourDflt;
    //----------------------------------------------------------------------------------------------------------------------
    /// @brief Defualt roughness of our GGX BRDF
    //----------------------------------------------------------------------------------------------------------------------
    RtFloat m_roughnessDflt;
    //----------------------------------------------------------------------------------------------------------------------

};

extern "C" PRMANEXPORT RixBxdfFactory *CreateRixBxdfFactory(const char *hint)
{
    return new PxrGGXFactory();
}

extern "C" PRMANEXPORT void DestroyRixBxdfFactory(RixBxdfFactory *bxdf)
{
    delete (PxrGGXFactory *) bxdf;
}

/*-----------------------------------------------------------------------*/
PxrGGXFactory::PxrGGXFactory()
{
    m_colourDflt = RtColorRGB(.5f);
    m_roughnessDflt = 0.f;
}

PxrGGXFactory::~PxrGGXFactory()
{
}

// Init
//  should be called once per RIB-instance. We look for parameter name
//  errors, and "cache" an understanding of our graph-evaluation requirements
//  in the form of allocation sizes.
int
PxrGGXFactory::Init(RixContext &ctx, char const *pluginpath)
{
    return 0;
}

// Synchronize: delivers occasional status information
// from the renderer. Parameterlist contents depend upon the SyncMsg.
// This method is optional and the default implementation ignores all
// events.
void
PxrGGXFactory::Synchronize(RixContext &ctx, RixSCSyncMsg syncMsg,
                              RixParameterList const *parameterList)
{
    if (syncMsg == k_RixSCRenderBegin)
    {
        s_reflBlinnLobe = RixBXLookupLobeByName(ctx, false, true, true, false,
                                                  k_reflBlinnLobeId,
                                                  "Reflection");

        s_reflBlinnLobeTraits = RixBXLobeTraits(s_reflBlinnLobe);
     }
}

enum paramIds
{
    k_color,
    k_roughness,
    k_numParams
};

RixSCParamInfo const *
PxrGGXFactory::GetParamTable()
{
    // see .args file for comments, etc...
    static RixSCParamInfo s_ptable[] =
    {
        RixSCParamInfo("Color", k_RixSCColor),
        RixSCParamInfo("Roughness", k_RixSCFloat),
        RixSCParamInfo() // end of table
    };
    return &s_ptable[0];
}

// CreateInstanceData:
//    analyze plist to determine our response to GetOpacityHints.
//    Checks these inputs:
//          transmissionBehavior (value),
//          presence (networked)
int
PxrGGXFactory::CreateInstanceData(RixContext &ctx,
                                      char const *handle,
                                      RixParameterList const *plist,
                                      InstanceData *idata)
{
    RtUInt64 req = k_TriviallyOpaque;
    idata->data = (void *) req; // no memory allocated, overload pointer
    idata->freefunc = NULL;
    return 0;
}

int
PxrGGXFactory::GetInstanceHints(RtConstPointer instanceData) const
{
    // our instance data is the RixBxdfFactory::InstanceHints bitfield.
    InstanceHints const &hints = (InstanceHints const&) instanceData;
    return hints;
}

// Finalize:
//  companion to Init, called with the expectation that any data
//  allocated there will be released here.
void
PxrGGXFactory::Finalize(RixContext &)
{
}
RixBsdf *
PxrGGXFactory::BeginScatter(RixShadingContext const *sCtx,
                                RixBXLobeTraits const &lobesWanted,
                                RixSCShadingMode sm,
                                RtConstPointer instanceData)
{
    // Get all input data
    RtColorRGB const * color;
    RtFloat const * rough;
    sCtx->EvalParam(k_color, -1, &color, &m_colourDflt, true);
    sCtx->EvalParam(k_roughness, -1, &rough, &m_roughnessDflt, true);

    RixShadingContext::Allocator pool(sCtx);
    void *mem = pool.AllocForBxdf<PxrGGX>(1);

    // Must use placement new to set up the vtable properly
    PxrGGX *eval = new (mem) PxrGGX(sCtx, this, lobesWanted, color,rough);

    return eval;
}

void
PxrGGXFactory::EndScatter(RixBsdf *)
{
}
